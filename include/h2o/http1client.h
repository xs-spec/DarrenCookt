/*
 * Copyright (c) 2014 DeNA Co., Ltd.
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
#ifndef h2o__http1client_h
#define h2o__http1client_h

#ifdef __cplusplus
extern "C" {
#endif

#include "picohttpparser.h"
#include "h2o/memory.h"
#include "h2o/socket.h"
#include "h2o/socketpool.h"
#include "h2o/timeout.h"

typedef struct st_h2o_http1client_t h2o_http1client_t;

typedef int (*h2o_http1client_body_cb)(h2o_http1client_t *client, const char *errstr);
typedef h2o_http1client_body_cb (*h2o_http1client_head_cb)(h2o_http1client_t *client, const char *errstr, int minor_version,
                                                           int status, h2o_iovec_t msg, struct phr_header *headers,
                                                           size_t num_headers);
typedef h2o_http1client_head_cb (*h2o_http1client_connect_cb)(h2o_http1client_t *client, const char *errstr, h2o_iovec_t **reqbufs,
                                                              size_t *reqbufcnt, int *method_is_head);

typedef struct st_h2o_http1client_ctx_t {
    h2o_loop_t *loop;
    h2o_timeout_t *zero_timeout;
    h2o_timeout_t *io_timeout;
} h2o_http1client_ctx_t;

struct st_h2o_http1client_t {
    h2o_http1client_ctx_t *ctx;
    h2o_mem_pool_t *pool;
    struct {
        h2o_socketpool_t *pool;
        h2o_socketpool_connect_request_t *connect_req;
    } sockpool;
    h2o_socket_t *sock;
    void *data;
    union {
        h2o_http1client_connect_cb on_connect;
        h2o_http1client_head_cb on_head;
        h2o_http1client_body_cb on_body;
    } _cb;
    const char *_errstr;
    h2o_timeout_entry_t _timeout;
    int _method_is_head;
    int _can_keepalive;
    union {
        struct {
            size_t bytesleft;
        } content_length;
        struct {
            struct phr_chunked_decoder decoder;
            size_t bytes_decoded_in_buf;
        } chunked;
    } _body_decoder;
};

extern const char *const h2o_http1client_error_is_eos;

h2o_http1client_t *h2o_http1client_connect(h2o_http1client_ctx_t *ctx, h2o_mem_pool_t *pool, const char *host, uint16_t port,
                                           h2o_http1client_connect_cb cb);
h2o_http1client_t *h2o_http1client_connect_with_pool(h2o_http1client_ctx_t *ctx, h2o_mem_pool_t *pool, h2o_socketpool_t *sockpool,
                                                     h2o_http1client_connect_cb cb);
void h2o_http1client_cancel(h2o_http1client_t *client);

#ifdef __cplusplus
}
#endif

#endif
