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

struct errors_status_ctx {
    uint64_t agg_errors_http1[E_HTTP_MAX];
    uint64_t agg_errors_http2[H2O_HTTP2_ERROR_MAX];
};

static void errors_status_per_thread(void *priv, h2o_context_t *ctx)
{
    size_t i;
    struct errors_status_ctx *esc = priv;
    for (i = 0; i < E_HTTP_MAX; i++) {
        esc->agg_errors_http1[i] += ctx->emitted_errors[i];
    }
    for (i = 0; i < E_HTTP_MAX; i++) {
        esc->agg_errors_http2[i] += ctx->http2.emitted_errors[i];
    }
}

static void *errors_status_init(h2o_iovec_t *err)
{
    struct errors_status_ctx *ret;

    ret = h2o_mem_alloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));

    return ret;
}

static h2o_iovec_t errors_status_final(void *priv, h2o_globalconf_t *gconf, h2o_req_t *req)
{
    struct errors_status_ctx *esc = priv;
    h2o_iovec_t ret;

#define BUFSIZE (2*1024)
    ret.base = h2o_mem_alloc_pool(&req->pool, BUFSIZE);
    ret.len = snprintf(ret.base, BUFSIZE, ",\n"
                                          " \"http1-errors-400\": %" PRIu64 ",\n"
                                          " \"http1-errors-403\": %" PRIu64 ",\n"
                                          " \"http1-errors-404\": %" PRIu64 ",\n"
                                          " \"http1-errors-405\": %" PRIu64 ",\n"
                                          " \"http1-errors-416\": %" PRIu64 ",\n"
                                          " \"http1-errors-417\": %" PRIu64 ",\n"
                                          " \"http1-errors-500\": %" PRIu64 ",\n"
                                          " \"http1-errors-502\": %" PRIu64 ",\n"
                                          " \"http1-errors-503\": %" PRIu64 ",\n"
                                          " \"http2-errors-protocol\": %" PRIu64 ", \n"
                                          " \"http2-errors-internal\": %" PRIu64 ", \n"
                                          " \"http2-errors-flow_control\": %" PRIu64 ", \n"
                                          " \"http2-errors-settings_timeout\": %" PRIu64 ", \n"
                                          " \"http2-errors-stream_closed\": %" PRIu64 ", \n"
                                          " \"http2-errors-frame_size\": %" PRIu64 ", \n"
                                          " \"http2-errors-refused_stream\": %" PRIu64 ", \n"
                                          " \"http2-errors-cancel\": %" PRIu64 ", \n"
                                          " \"http2-errors-compression\": %" PRIu64 ", \n"
                                          " \"http2-errors-connect\": %" PRIu64 ", \n"
                                          " \"http2-errors-enhance_your_calm\": %" PRIu64 ", \n"
                                          " \"http2-errors-inadequate_security\": %" PRIu64 "\n",
                                          esc->agg_errors_http1[E_HTTP_400], esc->agg_errors_http1[E_HTTP_403],
                                          esc->agg_errors_http1[E_HTTP_404], esc->agg_errors_http1[E_HTTP_405],
                                          esc->agg_errors_http1[E_HTTP_416], esc->agg_errors_http1[E_HTTP_417],
                                          esc->agg_errors_http1[E_HTTP_500], esc->agg_errors_http1[E_HTTP_502],
                                          esc->agg_errors_http1[E_HTTP_503],
                                          esc->agg_errors_http2[-H2O_HTTP2_ERROR_PROTOCOL], esc->agg_errors_http2[-H2O_HTTP2_ERROR_INTERNAL],
                                          esc->agg_errors_http2[-H2O_HTTP2_ERROR_FLOW_CONTROL], esc->agg_errors_http2[-H2O_HTTP2_ERROR_SETTINGS_TIMEOUT],
                                          esc->agg_errors_http2[-H2O_HTTP2_ERROR_STREAM_CLOSED], esc->agg_errors_http2[-H2O_HTTP2_ERROR_FRAME_SIZE],
                                          esc->agg_errors_http2[-H2O_HTTP2_ERROR_REFUSED_STREAM], esc->agg_errors_http2[-H2O_HTTP2_ERROR_CANCEL],
                                          esc->agg_errors_http2[-H2O_HTTP2_ERROR_COMPRESSION], esc->agg_errors_http2[-H2O_HTTP2_ERROR_CONNECT],
                                          esc->agg_errors_http2[-H2O_HTTP2_ERROR_ENHANCE_YOUR_CALM], esc->agg_errors_http2[-H2O_HTTP2_ERROR_INADEQUATE_SECURITY]);
    free(esc);
    return ret;
#undef BUFSIZE
}

h2o_status_handler_t errors_status_handler = {
    { H2O_STRLIT("errors") },
    errors_status_init,
    errors_status_per_thread,
    errors_status_final,
};
