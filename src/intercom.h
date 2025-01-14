#pragma once

#include "clientmgr.h"
#include "pcmchunk.h"
#include "taskqueue.h"
#include "vector.h"
#include "stream.h"
#include "packet_types.h"
#include "pqueue.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PACKET_FORMAT_VERSION 1
#define CLIENTNAME_MAXLEN

enum { CLIENT_OPERATION, AUDIO_DATA, SERVER_OPERATION };  // Packet types
enum { REQUEST, HELLO };				  // TLV types client op
enum { AUDIO,
       AUDIO_PCM,
       AUDIO_OPUS,
       AUDIO_OGG,
       AUDIO_FLAC };				   // TLV type for AUDIO packets, AUDIO will be obsoleted by the more specific types once implemented.
enum { STREAM_INFO, CLIENT_STOP, CLIENT_VOLUME };  // TLV - types for server op

typedef struct __attribute__((__packed__)) {
	uint8_t version;
	uint8_t type;
	uint16_t clientid;
	uint32_t nonce;
} intercom_packet_hdr;

typedef struct __attribute__((__packed__)) {
	uint8_t type;
	uint8_t length;
} tlv_op;

typedef struct __attribute__((__packed__)) {
	uint8_t type;
	uint8_t length;
	uint32_t nonce;
} tlv_request;

typedef struct __attribute__((__packed__)) {
	uint8_t type;
	uint8_t length;
	uint32_t node_id;
	uint32_t latency;
	uint8_t volume;
} tlv_hello;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	// after this a dynamic buffer is appended to hold TLV
} intercom_packet_hello;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	// after this a dynamic buffer is appended to hold TLV.
} intercom_packet_op;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	// after this a dynamic buffer is appended to hold TLV.
} intercom_packet_sop;

typedef struct __attribute__((__packed__)) {
	intercom_packet_hdr hdr;
	uint16_t bufferms;
	// after this a dynamic buffer is appended to hold TLV.
} intercom_packet_audio;

typedef VECTOR(client_t) client_v;

struct intercom_task {
	uint16_t packet_len;
	uint8_t *packet;
	struct in6_addr *recipient;
	taskqueue_t *check_task;
	uint8_t retries_left;
};

struct buffer_cleanup_task {
	audio_packet ap;
};

typedef struct {
	struct in6_addr serverip;
	VECTOR(intercom_packet_hdr) recent_packets;
	VECTOR(audio_packet) missing_packets;
	int fd;
	uint16_t port;
	uint16_t controlport;
	uint32_t nodeid;
	int mtu;
	int buffer_wraparound;

	size_t lastreceviedseqno;
	PQueue *receivebuffer;
} intercom_ctx;

void intercom_send_audio(intercom_ctx *ctx, stream *s);
void intercom_recently_seen_add(intercom_ctx *ctx, intercom_packet_hdr *hdr);
bool intercom_send_packet_unicast(intercom_ctx *ctx, const struct in6_addr *recipient, uint8_t *packet, ssize_t packet_len, int port);
void intercom_seek(intercom_ctx *ctx, const struct in6_addr *address);
void intercom_init_unicast(intercom_ctx *ctx);
void intercom_init(intercom_ctx *ctx);
void intercom_handle_in(intercom_ctx *ctx, int fd);
bool intercom_hello(intercom_ctx *ctx, const struct in6_addr *recipient, const int port);
bool intercom_stop_client(intercom_ctx *ctx, const client_t *client);
bool intercom_set_volume(intercom_ctx *ctx, const client_t *client, uint8_t volume);

struct timespec intercom_get_time_next_audiochunk(intercom_ctx *ctx);

bool intercom_peeknextaudiochunk(intercom_ctx *ctx, pcmChunk **ret);
void intercom_getnextaudiochunk(intercom_ctx *ctx, pcmChunk *c);
