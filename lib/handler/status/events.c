/*
 * Copyright (c) 2016 Fastly
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "h2o.h"
#include <inttypes.h>

struct st_events_status_ctx_t {
    uint64_t emitted_status_errors[H2O_STATUS_ERROR_MAX];
    uint64_t ssl_errors;
    uint64_t ssl_alpn_h1;
    uint64_t ssl_alpn_h2;
    uint64_t ssl_handshake_full;
    uint64_t ssl_handshake_resume;
    uint64_t ssl_handshake_full_latency;
    uint64_t ssl_handshake_resume_latency;
    uint64_t h2_protocol_level_errors[H2O_HTTP2_ERROR_MAX];
    uint64_t h2_read_closed;
    uint64_t h2_write_closed;
    pthread_mutex_t mutex;
};

static void events_status_per_thread(void *priv, h2o_context_t *ctx)
{
    size_t i;
    struct st_events_status_ctx_t *esc = priv;

    pthread_mutex_lock(&esc->mutex);

    for (i = 0; i < H2O_STATUS_ERROR_MAX; i++) {
        esc->emitted_status_errors[i] += ctx->emitted_error_status[i];
    }
    esc->ssl_errors += ctx->ssl.events.errors;
    esc->ssl_alpn_h1 += ctx->ssl.events.alpn_h1;
    esc->ssl_alpn_h2 += ctx->ssl.events.alpn_h2;
    esc->ssl_handshake_full += ctx->ssl.events.handshake_full;
    esc->ssl_handshake_resume += ctx->ssl.events.handshake_resume;
    esc->ssl_handshake_full_latency += ctx->ssl.events.handshake_full_latency;
    esc->ssl_handshake_resume_latency += ctx->ssl.events.handshake_resume_latency;
    for (i = 0; i < H2O_HTTP2_ERROR_MAX; i++) {
        esc->h2_protocol_level_errors[i] += ctx->http2.events.protocol_level_errors[i];
    }
    esc->h2_read_closed += ctx->http2.events.read_closed;
    esc->h2_write_closed += ctx->http2.events.write_closed;

    pthread_mutex_unlock(&esc->mutex);
}

static void *events_status_init(void)
{
    struct st_events_status_ctx_t *ret;

    ret = h2o_mem_alloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));
    pthread_mutex_init(&ret->mutex, NULL);

    return ret;
}

static h2o_iovec_t events_status_final(void *priv, h2o_globalconf_t *gconf, h2o_req_t *req)
{
    struct st_events_status_ctx_t *esc = priv;
    h2o_iovec_t ret;

#define H1_AGG_ERR(status_) esc->emitted_status_errors[H2O_STATUS_ERROR_##status_]
#define H2_AGG_ERR(err_) esc->h2_protocol_level_errors[-H2O_HTTP2_ERROR_##err_]
#define BUFSIZE (2 * 1024)
    ret.base = h2o_mem_alloc_pool(&req->pool, char, BUFSIZE);
    ret.len = snprintf(ret.base, BUFSIZE, ",\n"
                                          " \"status-errors.400\": %" PRIu64 ",\n"
                                          " \"status-errors.403\": %" PRIu64 ",\n"
                                          " \"status-errors.404\": %" PRIu64 ",\n"
                                          " \"status-errors.405\": %" PRIu64 ",\n"
                                          " \"status-errors.416\": %" PRIu64 ",\n"
                                          " \"status-errors.417\": %" PRIu64 ",\n"
                                          " \"status-errors.500\": %" PRIu64 ",\n"
                                          " \"status-errors.502\": %" PRIu64 ",\n"
                                          " \"status-errors.503\": %" PRIu64 ",\n"
                                          " \"ssl-errors\": %" PRIu64 ",\n"
                                          " \"ssl.alpn.h1\": %" PRIu64 ",\n"
                                          " \"ssl.alpn.h2\": %" PRIu64 ",\n"
                                          " \"ssl.handshake.full\": %" PRIu64 ",\n"
                                          " \"ssl.handshake.resume\": %" PRIu64 ",\n"
                                          " \"ssl.handshake.full.latency\": %" PRIu64 ",\n"
                                          " \"ssl.handshake.resume.latency\": %" PRIu64 ",\n"
                                          " \"http2-errors.protocol\": %" PRIu64 ", \n"
                                          " \"http2-errors.internal\": %" PRIu64 ", \n"
                                          " \"http2-errors.flow-control\": %" PRIu64 ", \n"
                                          " \"http2-errors.settings-timeout\": %" PRIu64 ", \n"
                                          " \"http2-errors.stream-closed\": %" PRIu64 ", \n"
                                          " \"http2-errors.frame-size\": %" PRIu64 ", \n"
                                          " \"http2-errors.refused-stream\": %" PRIu64 ", \n"
                                          " \"http2-errors.cancel\": %" PRIu64 ", \n"
                                          " \"http2-errors.compression\": %" PRIu64 ", \n"
                                          " \"http2-errors.connect\": %" PRIu64 ", \n"
                                          " \"http2-errors.enhance-your-calm\": %" PRIu64 ", \n"
                                          " \"http2-errors.inadequate-security\": %" PRIu64 ", \n"
                                          " \"http2.read-closed\": %" PRIu64 ", \n"
                                          " \"http2.write-closed\": %" PRIu64 "\n",
                       H1_AGG_ERR(400), H1_AGG_ERR(403), H1_AGG_ERR(404), H1_AGG_ERR(405), H1_AGG_ERR(416), H1_AGG_ERR(417),
                       H1_AGG_ERR(500), H1_AGG_ERR(502), H1_AGG_ERR(503), esc->ssl_errors, esc->ssl_alpn_h1, esc->ssl_alpn_h2, esc->ssl_handshake_full, esc->ssl_handshake_resume, esc->ssl_handshake_full_latency, esc->ssl_handshake_resume_latency,  H2_AGG_ERR(PROTOCOL), H2_AGG_ERR(INTERNAL),
                       H2_AGG_ERR(FLOW_CONTROL), H2_AGG_ERR(SETTINGS_TIMEOUT), H2_AGG_ERR(STREAM_CLOSED), H2_AGG_ERR(FRAME_SIZE),
                       H2_AGG_ERR(REFUSED_STREAM), H2_AGG_ERR(CANCEL), H2_AGG_ERR(COMPRESSION), H2_AGG_ERR(CONNECT),
                       H2_AGG_ERR(ENHANCE_YOUR_CALM), H2_AGG_ERR(INADEQUATE_SECURITY), esc->h2_read_closed, esc->h2_write_closed);
    pthread_mutex_destroy(&esc->mutex);
    free(esc);
    return ret;
#undef BUFSIZE
#undef H1_AGG_ERR
#undef H2_AGG_ERR
}

h2o_status_handler_t h2o_events_status_handler = {
    {H2O_STRLIT("events")}, events_status_final, events_status_init, events_status_per_thread
};
