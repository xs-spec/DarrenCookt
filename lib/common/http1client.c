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
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "picohttpparser.h"
#include "h2o/string_.h"
#include "h2o/http1client.h"

struct st_h2o_http1client_private_t {
    h2o_http1client_t super;
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

static void close_client(struct st_h2o_http1client_private_t *client)
{
    if (client->super.sock != NULL) {
        if (client->super.sockpool.pool != NULL && client->_can_keepalive) {
            /* we do not send pipelined requests, and thus can trash all the received input at the end of the request */
            h2o_buffer_consume(&client->super.sock->input, client->super.sock->input->size);
            h2o_socketpool_return(client->super.sockpool.pool, client->super.sock);
        } else {
            h2o_socket_close(client->super.sock);
        }
    } else {
        if (client->super.sockpool.connect_req != NULL) {
            h2o_socketpool_cancel_connect(client->super.sockpool.connect_req);
            client->super.sockpool.connect_req = NULL;
        }
    }
    if (h2o_timeout_is_linked(&client->_timeout))
        h2o_timeout_unlink(&client->_timeout);
    free(client);
}

static void on_body_error(struct st_h2o_http1client_private_t *client, const char *errstr)
{
    client->_can_keepalive = 0;
    client->_cb.on_body(&client->super, errstr);
    close_client(client);
}

static void on_body_timeout(h2o_timeout_entry_t *entry)
{
    struct st_h2o_http1client_private_t *client = H2O_STRUCT_FROM_MEMBER(struct st_h2o_http1client_private_t, _timeout, entry);
    on_body_error(client, "I/O timeout");
}

static void on_body_until_close(h2o_socket_t *sock, int status)
{
    struct st_h2o_http1client_private_t *client = sock->data;

    h2o_timeout_unlink(&client->_timeout);

    if (status != 0) {
        client->_cb.on_body(&client->super, h2o_http1client_error_is_eos);
        close_client(client);
        return;
    }

    if (sock->bytes_read != 0) {
        if (client->_cb.on_body(&client->super, NULL) != 0) {
            close_client(client);
            return;
        }
        h2o_buffer_consume(&sock->input, sock->input->size);
    }

    h2o_timeout_link(client->super.ctx->loop, client->super.ctx->io_timeout, &client->_timeout);
}

static void on_body_content_length(h2o_socket_t *sock, int status)
{
    struct st_h2o_http1client_private_t *client = sock->data;

    h2o_timeout_unlink(&client->_timeout);

    if (status != 0) {
        on_body_error(client, "I/O error (body; content-length)");
        return;
    }

    if (sock->bytes_read != 0) {
        const char *errstr;
        int ret;
        if (client->_body_decoder.content_length.bytesleft <= sock->bytes_read) {
            if (client->_body_decoder.content_length.bytesleft < sock->bytes_read) {
                /* remove the trailing garbage from buf, and disable keepalive */
                client->super.sock->input->size -= sock->bytes_read - client->_body_decoder.content_length.bytesleft;
                client->_can_keepalive = 0;
            }
            client->_body_decoder.content_length.bytesleft = 0;
            errstr = h2o_http1client_error_is_eos;
        } else {
            client->_body_decoder.content_length.bytesleft -= sock->bytes_read;
            errstr = NULL;
        }
        ret = client->_cb.on_body(&client->super, errstr);
        if (errstr == h2o_http1client_error_is_eos) {
            close_client(client);
            return;
        } else if (ret != 0) {
            client->_can_keepalive = 0;
            close_client(client);
            return;
        }
    }

    h2o_timeout_link(client->super.ctx->loop, client->super.ctx->io_timeout, &client->_timeout);
}

static void on_body_chunked(h2o_socket_t *sock, int status)
{
    struct st_h2o_http1client_private_t *client = sock->data;
    h2o_buffer_t *inbuf;

    h2o_timeout_unlink(&client->_timeout);

    if (status != 0) {
        on_body_error(client, "I/O error (body; chunked)");
        return;
    }

    inbuf = client->super.sock->input;
    if (sock->bytes_read != 0) {
        const char *errstr;
        int cb_ret;
        size_t newsz = sock->bytes_read;
        switch (phr_decode_chunked(&client->_body_decoder.chunked.decoder, inbuf->bytes + inbuf->size - newsz, &newsz)) {
        case -1: /* error */
            newsz = sock->bytes_read;
            client->_can_keepalive = 0;
            errstr = "failed to parse the response (chunked)";
            break;
        case -2: /* incomplete */
            errstr = NULL;
            break;
        default: /* complete, with garbage on tail; should disable keepalive */
            client->_can_keepalive = 0;
        /* fallthru */
        case 0: /* complete */
            errstr = h2o_http1client_error_is_eos;
            break;
        }
        inbuf->size -= sock->bytes_read - newsz;
        cb_ret = client->_cb.on_body(&client->super, errstr);
        if (errstr != NULL) {
            close_client(client);
            return;
        } else if (cb_ret != 0) {
            client->_can_keepalive = 0;
            close_client(client);
            return;
        }
    }

    h2o_timeout_link(client->super.ctx->loop, client->super.ctx->io_timeout, &client->_timeout);
}

static void on_error_before_head(struct st_h2o_http1client_private_t *client, const char *errstr)
{
    assert(!client->_can_keepalive);
    client->_cb.on_head(&client->super, errstr, 0, 0, h2o_iovec_init(NULL, 0), NULL, 0);
    close_client(client);
}

static void on_head(h2o_socket_t *sock, int status)
{
    struct st_h2o_http1client_private_t *client = sock->data;
    int minor_version, http_status, rlen, is_eos;
    const char *msg;
    struct phr_header headers[100];
    size_t msg_len, num_headers, i;
    h2o_socket_cb reader;

    h2o_timeout_unlink(&client->_timeout);

    if (status != 0) {
        on_error_before_head(client, "I/O error (head)");
        return;
    }

    /* parse response */
    num_headers = sizeof(headers) / sizeof(headers[0]);
    rlen = phr_parse_response(sock->input->bytes, sock->input->size, &minor_version, &http_status, &msg, &msg_len, headers,
                              &num_headers, 0);
    switch (rlen) {
    case -1: /* error */
        on_error_before_head(client, "failed to parse the response");
        return;
    case -2: /* incomplete */
        h2o_timeout_link(client->super.ctx->loop, client->super.ctx->io_timeout, &client->_timeout);
        return;
    }

    /* parse the headers */
    reader = on_body_until_close;
    client->_can_keepalive = minor_version >= 1;
    for (i = 0; i != num_headers; ++i) {
        h2o_strtolower((char *)headers[i].name, headers[i].name_len);
        if (h2o_memis(headers[i].name, headers[i].name_len, H2O_STRLIT("connection"))) {
            if (h2o_contains_token(headers[i].value, headers[i].value_len, H2O_STRLIT("keep-alive"), ',')) {
                client->_can_keepalive = 1;
            } else {
                client->_can_keepalive = 0;
            }
        } else if (h2o_memis(headers[i].name, headers[i].name_len, H2O_STRLIT("transfer-encoding"))) {
            if (h2o_memis(headers[i].value, headers[i].value_len, H2O_STRLIT("chunked"))) {
                /* precond: _body_decoder.chunked is zero-filled */
                client->_body_decoder.chunked.decoder.consume_trailer = 1;
                reader = on_body_chunked;
            } else if (h2o_memis(headers[i].value, headers[i].value_len, H2O_STRLIT("identity"))) {
                /* continue */
            } else {
                on_error_before_head(client, "unexpected type of transfer-encoding");
                return;
            }
        } else if (h2o_memis(headers[i].name, headers[i].name_len, H2O_STRLIT("content-length"))) {
            if ((client->_body_decoder.content_length.bytesleft = h2o_strtosize(headers[i].value, headers[i].value_len)) ==
                SIZE_MAX) {
                on_error_before_head(client, "invalid content-length");
                return;
            }
            if (reader != on_body_chunked)
                reader = on_body_content_length;
        }
    }
    /* close the connection if impossible to determine the end of the response (RFC 7230 3.3.3) */
    if (reader == on_body_until_close)
        client->_can_keepalive = 0;

    /* RFC 2616 4.4 */
    if (client->_method_is_head || ((100 <= status && status <= 199) || status == 204 || status == 304)) {
        is_eos = 1;
    } else {
        is_eos = 0;
    }

    /* call the callback */
    client->_cb.on_body = client->_cb.on_head(&client->super, is_eos ? h2o_http1client_error_is_eos : NULL, minor_version,
                                              http_status, h2o_iovec_init(msg, msg_len), headers, num_headers);
    if (is_eos) {
        close_client(client);
        return;
    } else if (client->_cb.on_body == NULL) {
        client->_can_keepalive = 0;
        close_client(client);
        return;
    }

    h2o_buffer_consume(&client->super.sock->input, rlen);
    client->super.sock->bytes_read = client->super.sock->input->size;

    client->_timeout.cb = on_body_timeout;
    h2o_socket_read_start(sock, reader);
    reader(client->super.sock, 0);
}

static void on_head_timeout(h2o_timeout_entry_t *entry)
{
    struct st_h2o_http1client_private_t *client = H2O_STRUCT_FROM_MEMBER(struct st_h2o_http1client_private_t, _timeout, entry);
    on_error_before_head(client, "I/O timeout");
}

static void on_send_request(h2o_socket_t *sock, int status)
{
    struct st_h2o_http1client_private_t *client = sock->data;

    h2o_timeout_unlink(&client->_timeout);

    if (status != 0) {
        on_error_before_head(client, "I/O error (send request)");
        return;
    }

    h2o_socket_read_start(client->super.sock, on_head);
    client->_timeout.cb = on_head_timeout;
    h2o_timeout_link(client->super.ctx->loop, client->super.ctx->io_timeout, &client->_timeout);
}

static void on_send_timeout(h2o_timeout_entry_t *entry)
{
    struct st_h2o_http1client_private_t *client = H2O_STRUCT_FROM_MEMBER(struct st_h2o_http1client_private_t, _timeout, entry);
    on_error_before_head(client, "I/O timeout");
}

static void on_connect_error(struct st_h2o_http1client_private_t *client, const char *errstr)
{
    assert(errstr != NULL);
    client->_cb.on_connect(&client->super, errstr, NULL, NULL, NULL);
    close_client(client);
}

static void on_connect(h2o_socket_t *sock, int status)
{
    struct st_h2o_http1client_private_t *client = sock->data;
    h2o_iovec_t *reqbufs;
    size_t reqbufcnt;

    h2o_timeout_unlink(&client->_timeout);

    if (status != 0) {
        on_connect_error(client, "connection failed");
        return;
    }

    if ((client->_cb.on_head = client->_cb.on_connect(&client->super, NULL, &reqbufs, &reqbufcnt, &client->_method_is_head)) ==
        NULL) {
        close_client(client);
        return;
    }
    h2o_socket_write(client->super.sock, reqbufs, reqbufcnt, on_send_request);
    /* TODO no need to set the timeout if all data has been written into TCP sendbuf */
    client->_timeout.cb = on_send_timeout;
    h2o_timeout_link(client->super.ctx->loop, client->super.ctx->io_timeout, &client->_timeout);
}

static void on_pool_connect(h2o_socket_t *sock, const char *errstr, void *data)
{
    struct st_h2o_http1client_private_t *client = data;

    client->super.sockpool.connect_req = NULL;

    if (sock == NULL) {
        assert(errstr != NULL);
        on_connect_error(client, errstr);
        return;
    }

    client->super.sock = sock;
    sock->data = client;
    on_connect(sock, 0);
}

static void on_connect_timeout(h2o_timeout_entry_t *entry)
{
    struct st_h2o_http1client_private_t *client = H2O_STRUCT_FROM_MEMBER(struct st_h2o_http1client_private_t, _timeout, entry);
    on_connect_error(client, client->_errstr != NULL ? client->_errstr : "connection timeout");
}

static struct st_h2o_http1client_private_t *create_client(h2o_http1client_ctx_t *ctx, h2o_mem_pool_t *pool,
                                                          h2o_http1client_connect_cb cb)
{
    struct st_h2o_http1client_private_t *client = h2o_mem_alloc(sizeof(*client));

    *client = (struct st_h2o_http1client_private_t){{ctx, pool}};
    client->_cb.on_connect = cb;
    /* caller needs to setup _cb, timeout.cb, sock, and sock->data */

    return client;
}

const char *const h2o_http1client_error_is_eos = "end of stream";

h2o_http1client_t *h2o_http1client_connect(h2o_http1client_ctx_t *ctx, h2o_mem_pool_t *pool, const char *host, uint16_t port,
                                           h2o_http1client_connect_cb cb)
{
    struct st_h2o_http1client_private_t *client;
    struct addrinfo hints, *res;
    char serv[sizeof("65535")];
    int err;

    /* setup */
    client = create_client(ctx, pool, cb);
    client->_timeout.cb = on_connect_timeout;
    /* resolve destination (FIXME use the function supplied by the loop) */
    sprintf(serv, "%u", (unsigned)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
    if ((err = getaddrinfo(host, serv, &hints, &res)) != 0) {
        client->_errstr = "name resulution failure";
        goto Error;
    }
    /* start connecting */
    client->super.sock = h2o_socket_connect(ctx->loop, res->ai_addr, res->ai_addrlen, on_connect);
    freeaddrinfo(res);
    if (client->super.sock == NULL) {
        client->_errstr = "socket create error";
        goto Error;
    }
    client->super.sock->data = client;
    h2o_timeout_link(ctx->loop, ctx->io_timeout, &client->_timeout);

    return &client->super;
Error:
    h2o_timeout_link(ctx->loop, ctx->zero_timeout, &client->_timeout);
    return &client->super;
}

h2o_http1client_t *h2o_http1client_connect_with_pool(h2o_http1client_ctx_t *ctx, h2o_mem_pool_t *pool, h2o_socketpool_t *sockpool,
                                                     h2o_http1client_connect_cb cb)
{
    struct st_h2o_http1client_private_t *client = create_client(ctx, pool, cb);
    client->super.sockpool.pool = sockpool;
    client->super.sockpool.connect_req = h2o_socketpool_connect(sockpool, ctx->loop, ctx->zero_timeout, on_pool_connect, client);
    client->_timeout.cb = on_connect_timeout;
    h2o_timeout_link(ctx->loop, ctx->io_timeout, &client->_timeout);
    return &client->super;
}

void h2o_http1client_cancel(h2o_http1client_t *_client)
{
    struct st_h2o_http1client_private_t *client = (void *)_client;
    client->_can_keepalive = 0;
    close_client(client);
}
