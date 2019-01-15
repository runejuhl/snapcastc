#include "alsaplayer.h"
#include "alloc.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "util.h"

#include <alsa/asoundlib.h>
#include <soxr.h>
#include <stdio.h>

#include "timespec.h"

#define PCM_DEVICE "default"
#define PERIOD_TIME 30000

// TODO: adjust speed without changing the pitch
void adjust_speed(pcmChunk *chunk, char *out, double factor) {
	double orate = chunk->samples * factor;
	size_t olen = (size_t)(chunk->size * orate / chunk->samples + .5);
	size_t odone;

	uint16_t inframes = chunk->size / chunk->channels / chunk->frame_size;
	uint16_t outframes = chunk->size / chunk->channels / chunk->frame_size * factor;
	if (inframes == outframes || chunk->play_at.tv_sec == 0) {
		// do not call sox for performance reasons when resampling would not have an effect
		log_error("skipping sox\n");
		memcpy(out, chunk->data, chunk->size);
		return;
	}

	soxr_quality_spec_t quality_spec = soxr_quality_spec(SOXR_VHQ, 0);
	soxr_io_spec_t io_spec = soxr_io_spec(SOXR_INT16_I, SOXR_INT16_I);  // TODO this should not be hard-coded.

	soxr_error_t error = soxr_oneshot(chunk->samples, orate, chunk->channels,				/* Rates and # of chans. */
					  chunk->data, chunk->size / chunk->channels / chunk->frame_size, NULL, /* Input. */
					  out, olen, &odone,							/* Output. */
					  &io_spec, &quality_spec, NULL);					/* Default configuration.*/

	log_error("len: %d, olen: %d odone: %d sox-error: %d\n", chunk->size, olen, odone, error);
	chunk->size = olen;
}

int getchunk(char *buf, int buffsize, size_t delay_frames) {
	const double adjustment = 0.01;
	double factor = 1;
	pcmChunk p;
	struct timespec ctime;
	obtainsystime(&ctime);
	struct timespec ts = ctime;

	int near_ms = 1;
	int not_even_close_ms = 500;

	struct timespec nextchunk_playat = intercom_get_time_next_audiochunk(&snapctx.intercom_ctx);

	size_t delay_ms_alsa = delay_frames * 1000 / snapctx.alsaplayer_ctx.rate;
	ts = timeAddMs(&ts, delay_ms_alsa);

	timediff tdiff = timeSub(&ts, &nextchunk_playat);

	bool is_near = (tdiff.time.tv_sec == 0 && tdiff.time.tv_nsec < near_ms * 1000000L);
	if (snapctx.alsaplayer_ctx.playing || ((!snapctx.alsaplayer_ctx.playing) && tdiff.sign > 0) || is_near) {
		p = intercom_getnextaudiochunk(&snapctx.intercom_ctx);
		if (chunk_is_empty(&p)) {
			snapctx.alsaplayer_ctx.empty_chunks_in_row++;
			if (snapctx.alsaplayer_ctx.empty_chunks_in_row > 5)
				snapctx.alsaplayer_ctx.playing = false;
		} else {
			snapctx.alsaplayer_ctx.playing = true;
			snapctx.alsaplayer_ctx.empty_chunks_in_row = 0;
		}
	} else
		get_emptychunk(&p);

	// TODO: should we adjust the factor based on how much the timing is off for faster sync on large buffers?
	if (!is_near) {
		factor = 1 - adjustment * tdiff.sign;
		bool not_even_close = (tdiff.time.tv_sec == 0 && tdiff.time.tv_nsec < not_even_close_ms * 1000000L);
		if (! not_even_close) {
			log_error("HAHA not even close, dropping chunk!\n");
			snapctx.alsaplayer_ctx.playing = false;
			snapctx.alsaplayer_ctx.empty_chunks_in_row = 0;
			return 0;
		}
	}

	// TODO: return a new chunk with new parameters instead of just a buffer.
	// TODO: for this make pcmChunk allow chunks with dynamic size.
	adjust_speed(&p, buf, factor);

	// TODO adjust volume

	log_verbose("status: %d chunk: buffsize: %d chunksize: %d current time: %s, play_at: %s difference: %s sign: %d\n",
		    snapctx.alsaplayer_ctx.playing, buffsize, p.size, print_timespec(&ctime), print_timespec(&p.play_at), print_timespec(&tdiff.time),
		    tdiff.sign);

	return p.size;
}

void alsaplayer_handle(alsaplayer_ctx *ctx) {
	unsigned int pcm;
	snd_pcm_sframes_t delayp;
	unsigned int buff_size = ctx->frames * ctx->frame_size * ctx->channels;
	unsigned int chunksize;

	if (snd_pcm_delay(ctx->pcm_handle, &delayp) < 0)
		log_error("could not obtain cm delay\n");

	if ((chunksize = getchunk(ctx->playnext, buff_size, delayp)) == 0) {
		log_error("end of data\n");  // TODO: schedule job to close alsa socket in alsatimeout ms. - still keep the sleep to reduce cpu
		return;
	}

	if ((pcm = snd_pcm_writei(ctx->pcm_handle, ctx->playnext, chunksize / ctx->channels / ctx->frame_size)) == -EPIPE) {
		log_error("XRUN.\n");
		snd_pcm_prepare(ctx->pcm_handle);
	} else if (pcm < 0) {
		log_error("ERROR. Can't write to PCM device. %s, snd_pcm_recover(%d)\n", snd_strerror(pcm),
			  (int)snd_pcm_recover(ctx->pcm_handle, pcm, 0));
	} else if (pcm < chunksize / ctx->channels / ctx->frame_size) {
		log_error("ERROR. write to pcm was not successful for all the data - THIS LIKELY IS A BUG");  // TODO: should we write the rest of the
													      // data later?
	}

	log_verbose("PCM delay frames: %d\n", delayp);
}

void alsaplayer_uninit(alsaplayer_ctx *ctx) {
	if (!ctx->initialized)
		return;
	snd_pcm_drain(ctx->pcm_handle);
	snd_pcm_close(ctx->pcm_handle);
	ctx->initialized = ctx->playing = false;
	free(ctx->ufds);
}

/*
vector<PcmDevice> AlsaPlayer::pcm_list(void) {
	void **hints, **n;
	char *name, *descr, *io;
	vector<PcmDevice> result;
	PcmDevice pcmDevice;

	if (snd_device_name_hint(-1, "pcm", &hints) < 0)
		return result;
	n = hints;
	size_t idx(0);
	while (*n != NULL) {
		name = snd_device_name_get_hint(*n, "NAME");
		descr = snd_device_name_get_hint(*n, "DESC");
		io = snd_device_name_get_hint(*n, "IOID");
		if (io != NULL && strcmp(io, "Output") != 0)
			goto __end;
		pcmDevice.name = name;
		if (descr == NULL) {
			pcmDevice.description = "";
		} else {
			pcmDevice.description = descr;
		}
		pcmDevice.idx = idx++;
		result.push_back(pcmDevice);

	__end:
		if (name != NULL)
			free(name);
		if (descr != NULL)
			free(descr);
		if (io != NULL)
			free(io);
		n++;
	}
	snd_device_name_free_hint(hints);
	return result;
}
*/
void alsaplayer_init(alsaplayer_ctx *ctx) {
	unsigned int pcm, tmp;
	int err;

	ctx->empty_chunks_in_row = 0;
	ctx->playing = false;

	if (ctx->initialized)
		return;

	// TODO: playnext is to be used from outside. allocation should not happen here.
	// FIXME: the size of that buffer is questionable - it is 1s - the amount of frames to be played is different
	ctx->playnext = snap_alloc(ctx->rate * ctx->channels * ctx->frame_size);

	int buff_size;

	ctx->pcm_handle = NULL;
	if ((pcm = snd_pcm_open(&ctx->pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
		log_error("ERROR: Cannot open \"%s\" PCM device. %s\n", PCM_DEVICE, snd_strerror(pcm));

	snd_pcm_hw_params_alloca(&ctx->params);
	snd_pcm_hw_params_any(ctx->pcm_handle, ctx->params);

	if ((pcm = snd_pcm_hw_params_set_access(ctx->pcm_handle, ctx->params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		log_error("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

	snd_pcm_format_t snd_pcm_format;
	if (ctx->frame_size == 1)
		snd_pcm_format = SND_PCM_FORMAT_S8;
	else if (ctx->frame_size == 2)
		snd_pcm_format = SND_PCM_FORMAT_S16_LE;
	else if (ctx->frame_size == 3)
		snd_pcm_format = SND_PCM_FORMAT_S24_LE;
	else if (ctx->frame_size == 4)
		snd_pcm_format = SND_PCM_FORMAT_S32_LE;
	else
		exit_error("unsupported format\n");

	if ((pcm = snd_pcm_hw_params_set_format(ctx->pcm_handle, ctx->params, snd_pcm_format)) < 0)
		log_error("ERROR: Can't set format. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_channels(ctx->pcm_handle, ctx->params, ctx->channels)) < 0)
		log_error("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_rate_near(ctx->pcm_handle, ctx->params, &ctx->rate, 0)) < 0)
		log_error("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

	snd_pcm_hw_params_get_period_time_max(ctx->params, &tmp, 0);
	if (tmp > PERIOD_TIME)
		tmp = PERIOD_TIME;

	unsigned int buffer_time = 4 * tmp;
	snd_pcm_hw_params_set_period_time_near(ctx->pcm_handle, ctx->params, &tmp, 0);
	snd_pcm_hw_params_set_buffer_time_near(ctx->pcm_handle, ctx->params, &buffer_time, 0);

	if ((pcm = snd_pcm_hw_params(ctx->pcm_handle, ctx->params)) < 0)
		log_error("ERROR: Can't set hardware parameters. %s\n", snd_strerror(pcm));

	log_verbose("PCM name: '%s'\n", snd_pcm_name(ctx->pcm_handle));
	log_verbose("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(ctx->pcm_handle)));

	snd_pcm_hw_params_get_channels(ctx->params, &tmp);
	log_verbose("channels: %i ", tmp);

	snd_pcm_hw_params_get_rate(ctx->params, &tmp, 0);
	log_verbose("rate: %d bps\n", tmp);

	snd_pcm_hw_params_get_period_size(ctx->params, &ctx->frames, 0);

	buff_size = ctx->frames * ctx->channels * ctx->frame_size /* 2 -> sample size */;
	log_verbose("alsa requested buff_size: %d\n", buff_size);

	snd_pcm_hw_params_get_period_time(ctx->params, &tmp, NULL);
	log_verbose("period time: %d\n", tmp);

	ctx->pollfd_count = snd_pcm_poll_descriptors_count(ctx->pcm_handle);
	assert(ctx->pollfd_count > 0);

	log_verbose("pollfd_count: %d\n", ctx->pollfd_count);
	ctx->ufds = snap_alloc(ctx->pollfd_count * sizeof(struct pollfd));

	if ((err = snd_pcm_poll_descriptors(ctx->pcm_handle, ctx->ufds, ctx->pollfd_count)) < 0) {
		exit_error("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
	}

	snd_pcm_sw_params_t *swparams;
	snd_pcm_sw_params_alloca(&swparams);
	snd_pcm_sw_params_current(ctx->pcm_handle, swparams);

	snd_pcm_sw_params_set_avail_min(ctx->pcm_handle, swparams, ctx->frames);
	snd_pcm_sw_params_set_start_threshold(ctx->pcm_handle, swparams, ctx->frames);
	snd_pcm_sw_params(ctx->pcm_handle, swparams);

	ctx->initialized = true;
}

bool is_alsafd(const int fd, const alsaplayer_ctx *ctx) {
	for (int i = 0; i < ctx->pollfd_count; i++) {
		struct pollfd *pfd = &ctx->ufds[i];

		if (fd == pfd->fd)
			return true;
	}

	return false;
}