/*
 * Copyright (c) 2018 Fastly, Kazuho Oku
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
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "picotls.h"
#include "picotls/openssl.h"
#include "quicly.h"
#include "h2o/hostinfo.h"
#include "h2o/httpclient.h"
#include "h2o/memory.h"
#include "h2o/multithread.h"
#include "h2o/http3_common.h"
#include "h2o/url.h"

static h2o_http3_ctx_t h3ctx;
static int num_requests_inflight;

static h2o_socket_t *create_socket(h2o_loop_t *loop)
{
    int fd;
    struct sockaddr_in sin;

    if ((fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    memset(&sin, 0, sizeof(sin));
    if (bind(fd, (void *)&sin, sizeof(sin)) != 0) {
        perror("bind");
        exit(1);
    }
    return h2o_evloop_socket_create(loop, fd, H2O_SOCKET_FLAG_DONT_READ);
}

static int on_body(h2o_httpclient_t *client, const char *errstr)
{
    if (errstr != NULL && errstr != h2o_httpclient_error_is_eos) {
        fprintf(stderr, "%s\n", errstr);
        exit(1);
        return -1;
    }

    fwrite((*client->buf)->bytes, 1, (*client->buf)->size, stdout);
    h2o_buffer_consume(&(*client->buf), (*client->buf)->size);

    if (errstr == h2o_httpclient_error_is_eos) {
        --num_requests_inflight;
        if (num_requests_inflight == 0)
            h2o_http3_close_all_connections(&h3ctx);
    }

    return 0;
}

static h2o_httpclient_body_cb on_head(h2o_httpclient_t *client, const char *errstr, int version, int status, h2o_iovec_t msg,
                                      h2o_header_t *headers, size_t num_headers, int header_requires_dup)
{
    size_t i;

    if (errstr != NULL && errstr != h2o_httpclient_error_is_eos) {
        fprintf(stderr, "%s\n", errstr);
        exit(1);
        return NULL;
    }

    printf("HTTP/3 %d %.*s\n", status, (int)msg.len, msg.base);
    for (i = 0; i != num_headers; ++i)
        printf("%.*s: %.*s\n", (int)headers[i].name->len, headers[i].name->base, (int)headers[i].value.len, headers[i].value.base);
    printf("\n");

    if (errstr == h2o_httpclient_error_is_eos) {
        fprintf(stderr, "no body\n");
        exit(1);
        return NULL;
    }

    return on_body;
}

static h2o_httpclient_head_cb on_connect(h2o_httpclient_t *client, const char *errstr, h2o_iovec_t *_method, h2o_url_t *url,
                                         const h2o_header_t **headers, size_t *num_headers, h2o_iovec_t *body,
                                         h2o_httpclient_proceed_req_cb *proceed_req_cb, h2o_httpclient_properties_t *props,
                                         h2o_url_t *origin)
{
    if (errstr != NULL) {
        fprintf(stderr, "%s\n", errstr);
        exit(1);
        return NULL;
    }

    *_method = h2o_iovec_init(H2O_STRLIT("GET"));
    *url = *((h2o_url_t *)client->data);
    *headers = NULL;
    *num_headers = 0;
    *body = h2o_iovec_init(NULL, 0);
    *proceed_req_cb = NULL;

    return on_head;
}

int main(int argc, char **argv)
{
    h2o_url_t url;
    h2o_multithread_receiver_t getaddr_receiver;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <url...>\n", argv[0]);
        exit(1);
    }

    h2o_loop_t *loop = h2o_evloop_create();
    h2o_multithread_queue_t *queue = h2o_multithread_create_queue(loop);
    h2o_multithread_register_receiver(queue, &getaddr_receiver, h2o_hostinfo_getaddr_receiver);
    h2o_socket_t *sock = create_socket(loop);

    ptls_context_t tlsctx = {ptls_openssl_random_bytes,
                             &ptls_get_time,
                             ptls_openssl_key_exchanges,
                             ptls_openssl_cipher_suites,
                             {NULL},
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             0,
                             0,
                             NULL,
                             1};
    quicly_amend_ptls_context(&tlsctx);
    quicly_context_t qctx = quicly_default_context;
    qctx.transport_params.max_streams_uni = 3;
    qctx.tls = &tlsctx;
    {
        uint8_t random_key[PTLS_SHA256_DIGEST_SIZE];
        tlsctx.random_bytes(random_key, sizeof(random_key));
        qctx.cid_encryptor = quicly_new_default_cid_encryptor(&ptls_openssl_bfecb, &ptls_openssl_sha256,
                                                              ptls_iovec_init(random_key, sizeof(random_key)));
        ptls_clear_memory(random_key, sizeof(random_key));
    }
    qctx.stream_open = &h2o_httpclient_http3_on_stream_open;
    // qctx.on_conn_close = h2o_hq_on_conn_close;
    qctx.event_log.cb = quicly_new_default_event_logger(stderr);
    qctx.event_log.mask = UINT64_MAX;

    h2o_http3_init_context(&h3ctx, loop, sock, &qctx, NULL);

    uint64_t io_timeout = 5000; /* 5 seconds */
    h2o_httpclient_ctx_t ctx = {loop, &getaddr_receiver, io_timeout, io_timeout, io_timeout,
                                NULL, io_timeout,        1048576,    {{0}},      &h3ctx};

    h2o_mem_pool_t pool;
    h2o_mem_init_pool(&pool);

    int i;
    for (i = 1; i != argc; ++i) {
        if (h2o_url_parse(argv[i], strlen(argv[i]), &url) != 0) {
            fprintf(stderr, "cannot parse url:%s\n", argv[i]);
            exit(1);
        }
        h2o_httpclient_t *client;
        h2o_httpclient_connect_h3(&client, &pool, &url, &ctx, &url, on_connect);
        ++num_requests_inflight;
    }

    while (!(num_requests_inflight == 0 && h2o_http3_num_connections(&h3ctx) == 0))
        h2o_evloop_run(ctx.loop, INT32_MAX);

    h2o_http3_dispose_context(&h3ctx);

    return 0;
}
