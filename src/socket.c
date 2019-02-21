/*
   Copyright (c) 2017, Christof Schulze <christof@christofschulze.com>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
   */

#include "socket.h"
#include "clientmgr.h"
#include "error.h"
#include "intercom.h"
#include "jsonrpc.h"
#include "snapcast.h"
#include "util.h"

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <json-c/json.h>

#include "version.h"

#define CONTROLPROTOCOLVERSION 1

void json_object_print_and_put(int fd, json_object *obj) {
	log_verbose("%s\n", json_object_to_json_string_ext(obj, 0));
	dprintf(fd, "%s", json_object_to_json_string_ext(obj, 0));
	json_object_put(obj);
}

void json_build_host(json_object *host, const char *arch, const char *ip, const char *mac, const char *name, const char *os) {
	json_object_object_add(host, "arch", json_object_new_string(arch));
	json_object_object_add(host, "ip", json_object_new_string(ip));
	json_object_object_add(host, "mac", json_object_new_string(mac));
	json_object_object_add(host, "name", json_object_new_string(name));
	json_object_object_add(host, "os", json_object_new_string(os));
}

void jsonrpc_buildresult(json_object *in, int id, json_object *result) {
	json_object_object_add(in, "id", json_object_new_int(id));
	json_object_object_add(in, "jsonrpc", json_object_new_string("2.0"));
	json_object_object_add(in, "result", result);
}

void client_status(json_object *in) {}

bool group_get_muted() {
	// TODO implement me
	return false;
}
const char *group_get_id() { return "2427bc26-e219-8d33-901c-20493f46eb42"; }

const char *group_get_name() { return ""; }

const char *group_get_stream_id() {
	// TODO implement me
	return "default";
}

void json_add_time(json_object *out, struct timespec *t) {
	json_object_object_add(out, "sec", json_object_new_int(t->tv_sec));
	json_object_object_add(out, "usec", json_object_new_int(t->tv_nsec / 1000));
}

void json_add_group(json_object *out) {
	json_object *clients = json_object_new_array();

	// TODO: iterate over all clients in this group

	for (int i = VECTOR_LEN(snapctx.clientmgr_ctx.clients) - 1; i >= 0; --i) {
		struct client *c = &VECTOR_INDEX(snapctx.clientmgr_ctx.clients, i);

		json_object *client = json_object_new_object();
		json_object *config = json_object_new_object();
		json_object *volume = json_object_new_object();

		json_object_object_add(config, "instance", json_object_new_int(1));  // TODO: What is this value?
		json_object_object_add(config, "latency", json_object_new_int(c->latency));
		// json_object_object_add(config, "name", json_object_new_string(c->name));
		json_object_object_add(config, "name", json_object_new_string(""));
		json_object_object_add(config, "volume", volume);
		json_object_object_add(volume, "muted", json_object_new_boolean(c->muted));
		json_object_object_add(volume, "percent", json_object_new_int(c->volume_percent));

		json_object_object_add(client, "config", config);
		json_object_object_add(client, "connected", json_object_new_boolean(c->connected));

		json_object *host = json_object_new_object();
		// json_build_host(host, "unknown", print_ip(&c->ip), print_mac(c->mac), "", "unknown");
		json_build_host(host, "unknown", "::ffff:192.168.13.153", "60:57:18:88:d7:f9", "lappi", "Linux");
		json_object_object_add(client, "host", host);

		json_object_object_add(client, "id", json_object_new_string("lappi"));

		json_object *lastseen = json_object_new_object();
		json_add_time(lastseen, &c->lastseen);
		json_object_object_add(client, "lastSeen", lastseen);

		json_object *snapclient = json_object_new_object();
		json_object_object_add(snapclient, "name", json_object_new_string("Snapclient"));
		json_object_object_add(snapclient, "protocolVersion", json_object_new_int(c->protoversion));
		json_object_object_add(snapclient, "version", json_object_new_string("0.15.0"));

		json_object_object_add(client, "snapclient", snapclient);

		json_object_array_add(clients, client);
	}

	json_object_object_add(out, "clients", clients);
	json_object_object_add(out, "id", json_object_new_string(group_get_id()));
	json_object_object_add(out, "muted", json_object_new_boolean(group_get_muted()));
	json_object_object_add(out, "name", json_object_new_string(group_get_name()));
	json_object_object_add(out, "stream_id", json_object_new_string(group_get_stream_id()));
}

void json_add_groups(json_object *out) {
	json_object *groups = json_object_new_array();

	// TODO: iterate over all groups
	json_object *group = json_object_new_object();
	json_add_group(group);
	json_object_array_add(groups, group);

	json_object_object_add(out, "groups", groups);
}

void json_build_serverstatus_streams(json_object *in) {
	/* TODO IMPLEMENT THIS FUNCTION  - after implementing the data model
	** for streams
	"streams": [
	{
		"id": "default",
			"meta": {
				"STREAM": "default"
			},
			"status": "idle",
			"uri": {
				"fragment": "",
				"host": "",
				"path": "/tmp/snapfifo",
				"query": {
					"buffer_ms": "20",
					"codec": "pcm",
					"name": "default",
					"sampleformat": "48000:16:2",
					"timeout_ms": "1000"
				},
				"raw": "pipe:////tmp/snapfifo?buffer_ms=20&codec=pcm&name=default&sampleformat=48000:16:2&timeout_ms=1000",
				"scheme": "pipe"
			}
	}
	]
	*/

	json_object *streams = json_object_new_array();

	// for i in streams

	json_object *stream = json_object_new_object();
	json_object *meta = json_object_new_object();
	json_object *uri = json_object_new_object();
	json_object *query = json_object_new_object();
	json_object_object_add(stream, "id", json_object_new_string("default"));
	json_object_object_add(meta, "STREAM", json_object_new_string("default"));
	json_object_object_add(stream, "meta", meta);
	json_object_object_add(stream, "status", json_object_new_string("idle"));

	json_object_object_add(stream, "uri", uri);
	json_object_object_add(uri, "fragment", json_object_new_string(""));
	json_object_object_add(uri, "host", json_object_new_string(""));
	json_object_object_add(uri, "path", json_object_new_string("/tmp/snapfifo"));

	json_object_object_add(query, "buffer_ms", json_object_new_string("20"));
	json_object_object_add(query, "codec", json_object_new_string("ogg"));
	json_object_object_add(query, "name", json_object_new_string("default"));
	json_object_object_add(query, "sampleformat", json_object_new_string("48000:16:2"));
	json_object_object_add(query, "timeout_ms", json_object_new_string("1000"));
	json_object_object_add(uri, "query", query);

	json_object_object_add(
	    uri, "raw", json_object_new_string("pipe:////tmp/snapfifo?buffer_ms=20&codec=ogg&name=default&sampleformat=48000:16:2&timeout_ms=1000"));
	json_object_object_add(uri, "scheme", json_object_new_string("pipe"));

	json_object_array_add(streams, stream);

	json_object_object_add(in, "streams", streams);  // streams - this is at the very least poorly named
}

void json_build_serverstatus_server(json_object *in) {
	// TODO: There is too much data in the API that serves no visible purpose. Get rid of it.
	json_object *server = json_object_new_object();
	json_object *host = json_object_new_object();

	char hostname[NI_MAXHOST + 1];
	gethostname(hostname, NI_MAXHOST);
	json_build_host(host, "unknown_arch", "", "", hostname, "Linux");

	json_object *snapserver = json_object_new_object();
	json_object_object_add(snapserver, "controlProtocolVersion", json_object_new_int(CONTROLPROTOCOLVERSION));
	json_object_object_add(snapserver, "name", json_object_new_string("Snapserver"));
	json_object_object_add(snapserver, "protocolVersion", json_object_new_int(PACKET_FORMAT_VERSION));
	// json_object_object_add(snapserver, "version", json_object_new_string(SOURCE_VERSION));
	json_object_object_add(snapserver, "version", json_object_new_string("0.15.0"));

	json_object_object_add(server, "host", host);
	json_object_object_add(server, "snapserver", snapserver);
	json_object_object_add(in, "server", server);
}

void json_build_serverstatus(json_object *in) {
	json_object *response = json_object_new_object();
	json_add_groups(response);
	json_build_serverstatus_server(response);
	json_build_serverstatus_streams(response);
	json_object_object_add(in, "server", response);
}

void handle_server_getstatus(jsonrpc_request *request, int fd) {
	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();
	jsonrpc_buildresult(response, request->id, result);

	json_build_serverstatus(result);
	json_object_print_and_put(fd, response);
}

void handle_GetRPCVersion(jsonrpc_request *request, int fd) {
	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();
	jsonrpc_buildresult(response, request->id, result);

	json_object_object_add(result, "major", json_object_new_string(SOURCE_VERSION_MAJOR));
	json_object_object_add(result, "minor", json_object_new_string(SOURCE_VERSION_MINOR));
	json_object_object_add(result, "patch", json_object_new_string(SOURCE_VERSION_PATCH));

	json_object_print_and_put(fd, response);
}

void socket_init(socket_ctx *ctx) {
	log_verbose("Initializing socket: %d\n", ctx->port);

	struct sockaddr_in6 server_addr = {
	    .sin6_family = AF_INET6, .sin6_port = htons(ctx->port),
	};

	ctx->fd = socket(PF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (ctx->fd < 0)
		exit_errno("creating API socket on node-IP");

	if (setsockopt(ctx->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
		exit_error("setsockopt(SO_REUSEADDR) failed");

	if (bind(ctx->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		exit_errno("bind socket to node-IP failed");
	}

	if (listen(ctx->fd, 5) < 0)
		exit_errno("Could not listen on status socket");
}

void handle_client_setvolume(jsonrpc_request *request, int fd) {
	// Request: {"id":8,"jsonrpc":"2.0","method":"Client.SetVolume","params":{"id":"00:21:6a:7d:74:fc","volume":{"muted":false,"percent":74}}}
	// Response: {"id":8,"jsonrpc":"2.0","result":{"volume":{"muted":false,"percent":74}}}

	json_object *response = json_object_new_object();
	json_object *result = json_object_new_object();
	jsonrpc_buildresult(response, request->id, result);

	int clientid = 0;
	json_object *jobj;
	json_object *mute;
	json_object *vol_percent;
	bool bool_result = false;

	for (int i = VECTOR_LEN(request->parameters) - 1; i >= 0; --i) {
		parameter *p = &VECTOR_INDEX(request->parameters, i);
		if (!strncmp(p->name, "id", 2))
			clientid = p->value.number;
		else if (!strncmp(p->name, "volume", 7)) {
			jobj = json_tokener_parse(p->value.json_string);

			if (!jobj) {
				log_error("error parsing json %s\n", p->value.json_string);
				goto reply;
			}

			if (!json_object_object_get_ex(jobj, "percent", &vol_percent) && !json_object_object_get_ex(jobj, "muted", &mute)) {
				log_error("Either mute or volume are mandatory in a volume object. We received %s\n", p->value.json_string);
				goto reply;
			}
		}
	}

	// TODO: implement volume change
	if (mute)
		bool_result = clientmgr_client_setmute(clientid, json_object_get_boolean(mute));

	json_object_put(mute);
	json_object_put(vol_percent);
	json_object_put(jobj);

// TODO: for compatibility with snapcast, return the result as described in the api
reply:
	json_object_object_add(result, "status", json_object_new_boolean(bool_result));
	json_object_print_and_put(fd, response);
}

int handle_request(jsonrpc_request *request, int fd) {
	if (!strncmp(request->method, "Server.GetRPCVersion", 20)) {
		handle_GetRPCVersion(request, fd);
	} else if (!strncmp(request->method, "Client.SetVolume", 16)) {
		log_debug("calling server Client.SetVolume\n");
		handle_client_setvolume(request, fd);
	} else if (!strncmp(request->method, "Server.GetStatus", 16)) {
		log_debug("calling server getstatus\n");
		handle_server_getstatus(request, fd);
	}
	return 1;
}

void stringprocessor(jsonrpc_request *request) {
	if (!strncmp(request->method, "Server.GetRPCVersion", 20)) {
		log_error("calling getrpcversion\n");
	} else if (!strncmp(request->method, "get_prefixes", 12)) {
		log_error("belanglose Nachricht\n");
	}

	return;
}

void socket_client_remove(socket_ctx *ctx, socketclient *sc) {
	close(sc->fd);
	VECTOR_DELETE(ctx->clients, VECTOR_GETINDEX(ctx->clients, sc));
}

int handle_line(socketclient *sc) {
	jsonrpc_request jreq = {};
	if (!(jsonrpc_parse_string(&jreq, sc->line))) {
		log_error("parsing unsuccessful for %s\n", sc->line);
		return false;
	}

	log_debug("parsing successful\n");

	int ret = handle_request(&jreq, sc->fd);

	jsonrpc_free_members(&jreq);
	return ret;
}

int socket_handle_client(socket_ctx *ctx, socketclient *sc) {
	int len = 0;

	log_debug("handling client\n");

	if (sc->line_offset < LINEBUFFER_SIZE) {
		len = read(sc->fd, &(sc->line[sc->line_offset]), LINEBUFFER_SIZE - sc->line_offset - 1);
		log_error("read: %s length: %d offset %d\n", sc->line, len, sc->line_offset);
		if (len > 0) {
			for (int i = sc->line_offset; i < LINEBUFFER_SIZE - 1; ++i) {
				if (sc->line[i] == '\n' || sc->line[i] == '\r') {
					sc->line[sc->line_offset + len] = '\0';
					log_debug("read full line: %s", sc->line);
					if (handle_line(sc) < 0) {
						// clients sending invalid data are getting their connection closed.
						return -1;
					};

					sc->line[0] = '\0';
					sc->line_offset = 0;
					return 0;
				}
			}

			sc->line_offset += len;
			return 0;
		} else if (len == 0) {
			log_debug("received EOF from client %d, closing socket\n", sc->fd);
			return -1;
		} else {
			log_error("jo\n");
			perror("Error when reading:");
			if (errno == EAGAIN) {
				log_debug("No more data, try again\n");
				return 0;
			}
			return -1;
		}
	} else {
		log_error("linebuffer for client exhausted. Closing.\n");
		return -1;
	}
}

bool socket_get_client(socket_ctx *ctx, socketclient **sc_dest, int fd) {
	for (int i = VECTOR_LEN(ctx->clients) - 1; i >= 0; --i) {
		socketclient *sc = &VECTOR_INDEX(ctx->clients, i);
		if (fd == sc->fd) {
			if (sc_dest)
				*sc_dest = sc;

			return true;
		}
	}
	return false;
}

int socket_handle_in(socket_ctx *ctx) {
	log_error("handling socket event\n");

	int fd = accept(ctx->fd, NULL, NULL);
	int flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
		exit_errno("could not set socket to non-blocking\n");
	}

	socketclient sc = {.fd = fd, .line_offset = 0, .line[0] = '\0'};
	VECTOR_ADD(ctx->clients, sc);

	// TODO: send all relevant notifications to this client.

	return fd;
}