/*
 * Copyright (c) 2014,2015 DeNA Co., Ltd., Kazuho Oku, Ryosuke Matsumoto
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
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include "h2o.h"
#include "h2o/mruby_.h"

#define STATUS_FALLTHRU 399

enum {
    LIT_REQUEST_METHOD = H2O_MAX_TOKENS,
    LIT_SCRIPT_NAME,
    LIT_PATH_INFO,
    LIT_QUERY_STRING,
    LIT_SERVER_NAME,
    LIT_SERVER_ADDR,
    LIT_SERVER_PORT,
    LIT_CONTENT_LENGTH,
    LIT_REMOTE_ADDR,
    LIT_REMOTE_PORT,
    LIT_RACK_URL_SCHEME,
    LIT_RACK_MULTITHREAD,
    LIT_RACK_MULTIPROCESS,
    LIT_RACK_RUN_ONCE,
    LIT_RACK_HIJACK_,
    LIT_RACK_ERRORS,
    LIT_SERVER_SOFTWARE,
    LIT_SERVER_SOFTWARE_VALUE,
    PROC_EACH_TO_ARRAY,
    NUM_CONSTANTS
};

typedef struct st_h2o_mruby_context_t {
    h2o_mruby_handler_t *handler;
    mrb_state *mrb;
    /* TODO: add other hook code */
    mrb_value proc;
    mrb_value constants;
    struct {
        mrb_sym sym_call;
        mrb_sym sym_close;
    } symbols;
} h2o_mruby_context_t;

#define FREEZE_STRING(v) RSTR_SET_FROZEN_FLAG(mrb_str_ptr(v))

mrb_value h2o_mruby_compile_code(mrb_state *mrb, h2o_mruby_config_vars_t *config, char *errbuf)
{
    mrbc_context *cxt;
    struct mrb_parser_state *parser;
    struct RProc *proc = NULL;
    mrb_value result = mrb_nil_value();

    /* parse */
    if ((cxt = mrbc_context_new(mrb)) == NULL) {
        fprintf(stderr, "%s: no memory\n", H2O_MRUBY_MODULE_NAME);
        abort();
    }
    if (config->path != NULL)
        mrbc_filename(mrb, cxt, config->path);
    cxt->capture_errors = 1;
    cxt->lineno = config->lineno;
    if ((parser = mrb_parse_nstring(mrb, config->source.base, (mrb_int)config->source.len, cxt)) == NULL) {
        fprintf(stderr, "%s: no memory\n", H2O_MRUBY_MODULE_NAME);
        abort();
    }
    /* return erro if errbuf is supplied, or abort */
    if (parser->nerr != 0) {
        if (errbuf == NULL) {
            fprintf(stderr, "%s: internal error (unexpected state)\n", H2O_MRUBY_MODULE_NAME);
            abort();
        }
        snprintf(errbuf, 256, "line %d:%s", parser->error_buffer[0].lineno, parser->error_buffer[0].message);
        goto Exit;
    }
    /* generate code */
    if ((proc = mrb_generate_code(mrb, parser)) == NULL) {
        fprintf(stderr, "%s: internal error (mrb_generate_code failed)\n", H2O_MRUBY_MODULE_NAME);
        abort();
    }

    result = mrb_run(mrb, proc, mrb_top_self(mrb));
    if (mrb->exc != NULL) {
        mrb_value obj = mrb_funcall(mrb, mrb_obj_value(mrb->exc), "inspect", 0);
        struct RString *error = mrb_str_ptr(obj);
        snprintf(errbuf, 256, "%s", error->as.heap.ptr);
        mrb->exc = 0;
        result = mrb_nil_value();
        goto Exit;
    } else if (mrb_nil_p(result)) {
        snprintf(errbuf, 256, "returned value is not callable");
        goto Exit;
    }

Exit:
    mrb_parser_free(parser);
    mrbc_context_free(mrb, cxt);
    return result;
}

static h2o_iovec_t convert_header_name_to_env(h2o_mem_pool_t *pool, const char *name, size_t len)
{
#define KEY_PREFIX "HTTP_"
#define KEY_PREFIX_LEN (sizeof(KEY_PREFIX) - 1)

    h2o_iovec_t ret;

    ret.len = len + KEY_PREFIX_LEN;
    ret.base = h2o_mem_alloc_pool(pool, ret.len);

    memcpy(ret.base, KEY_PREFIX, KEY_PREFIX_LEN);

    char *d = ret.base + KEY_PREFIX_LEN;
    for (; len != 0; ++name, --len)
        *d++ = *name == '-' ? '_' : h2o_toupper(*name);

    return ret;

#undef KEY_PREFIX
#undef KEY_PREFIX_LEN
}

static mrb_value build_constants(mrb_state *mrb, const char *server_name, size_t server_name_len)
{
    mrb_value ary = mrb_ary_new_capa(mrb, NUM_CONSTANTS);
    mrb_int i;

    mrb_int arena = mrb_gc_arena_save(mrb);

    {
        h2o_mem_pool_t pool;
        h2o_mem_init_pool(&pool);
        for (i = 0; i != H2O_MAX_TOKENS; ++i) {
            const h2o_token_t *token = h2o__tokens + i;
            mrb_value lit = mrb_nil_value();
            if (token == H2O_TOKEN_CONTENT_TYPE) {
                lit = mrb_str_new_lit(mrb, "CONTENT_TYPE");
            } else if (token->buf.len != 0) {
                h2o_iovec_t n = convert_header_name_to_env(&pool, token->buf.base, token->buf.len);
                lit = mrb_str_new(mrb, n.base, n.len);
            }
            if (mrb_string_p(lit)) {
                FREEZE_STRING(lit);
                mrb_ary_set(mrb, ary, i, lit);
            }
            mrb_gc_arena_restore(mrb, arena);
        }
        h2o_mem_clear_pool(&pool);
    }

#define SET_STRING(idx, value)                                                                                                     \
    do {                                                                                                                           \
        mrb_value lit = (value);                                                                                                   \
        FREEZE_STRING(lit);                                                                                                        \
        mrb_ary_set(mrb, ary, idx, lit);                                                                                           \
        mrb_gc_arena_restore(mrb, arena);                                                                                          \
    } while (0)
#define SET_LITERAL(idx, str) SET_STRING(idx, mrb_str_new_lit(mrb, str))

    SET_LITERAL(LIT_REQUEST_METHOD, "REQUEST_METHOD");
    SET_LITERAL(LIT_SCRIPT_NAME, "SCRIPT_NAME");
    SET_LITERAL(LIT_PATH_INFO, "PATH_INFO");
    SET_LITERAL(LIT_QUERY_STRING, "QUERY_STRING");
    SET_LITERAL(LIT_SERVER_NAME, "SERVER_NAME");
    SET_LITERAL(LIT_SERVER_ADDR, "SERVER_ADDR");
    SET_LITERAL(LIT_SERVER_PORT, "SERVER_PORT");
    SET_LITERAL(LIT_CONTENT_LENGTH, "CONTENT_LENGTH");
    SET_LITERAL(LIT_REMOTE_ADDR, "REMOTE_ADDR");
    SET_LITERAL(LIT_REMOTE_PORT, "REMOTE_PORT");
    SET_LITERAL(LIT_RACK_URL_SCHEME, "rack.url_scheme");
    SET_LITERAL(LIT_RACK_MULTITHREAD, "rack.multithread");
    SET_LITERAL(LIT_RACK_MULTIPROCESS, "rack.multiprocess");
    SET_LITERAL(LIT_RACK_RUN_ONCE, "rack.run_once");
    SET_LITERAL(LIT_RACK_HIJACK_, "rack.hijack?");
    SET_LITERAL(LIT_RACK_ERRORS, "rack.errors");
    SET_LITERAL(LIT_SERVER_SOFTWARE, "SERVER_SOFTWARE");
    SET_STRING(LIT_SERVER_SOFTWARE_VALUE, mrb_str_new(mrb, server_name, server_name_len));

#undef SET_LITERAL
#undef SET_STRING

    mrb_ary_set(mrb, ary, PROC_EACH_TO_ARRAY,
                mrb_funcall(mrb, mrb_top_self(mrb), "eval", 1,
                            mrb_str_new_lit(mrb, "Proc.new do |o| a = []; o.each do |x| a << x; end; a; end")));

    return ary;
}

static void on_context_init(h2o_handler_t *_handler, h2o_context_t *ctx)
{
    h2o_mruby_handler_t *handler = (void *)_handler;
    h2o_mruby_context_t *handler_ctx = h2o_mem_alloc(sizeof(*handler_ctx));

    handler_ctx->handler = handler;

    /* init mruby in every thread */
    if ((handler_ctx->mrb = mrb_open()) == NULL) {
        fprintf(stderr, "%s: no memory\n", H2O_MRUBY_MODULE_NAME);
        abort();
    }

    /* compile code (must be done for each thread) */
    int arena = mrb_gc_arena_save(handler_ctx->mrb);
    handler_ctx->proc = h2o_mruby_compile_code(handler_ctx->mrb, &handler->config, NULL);
    mrb_gc_arena_restore(handler_ctx->mrb, arena);
    mrb_gc_protect(handler_ctx->mrb, handler_ctx->proc);

    handler_ctx->constants = build_constants(handler_ctx->mrb, ctx->globalconf->server_name.base, ctx->globalconf->server_name.len);
    handler_ctx->symbols.sym_call = mrb_intern_lit(handler_ctx->mrb, "call");
    handler_ctx->symbols.sym_close = mrb_intern_lit(handler_ctx->mrb, "close");

    h2o_context_set_handler_context(ctx, &handler->super, handler_ctx);
}

static void on_context_dispose(h2o_handler_t *_handler, h2o_context_t *ctx)
{
    h2o_mruby_handler_t *handler = (void *)_handler;
    h2o_mruby_context_t *handler_ctx = h2o_context_get_handler_context(ctx, &handler->super);

    if (handler_ctx == NULL)
        return;

    mrb_close(handler_ctx->mrb);
    free(handler_ctx);
}

static void on_handler_dispose(h2o_handler_t *_handler)
{
    h2o_mruby_handler_t *handler = (void *)_handler;

    free(handler->config.source.base);
    free(handler->config.path);
    free(handler);
}

static void report_exception(h2o_req_t *req, mrb_state *mrb)
{
    mrb_value obj = mrb_funcall(mrb, mrb_obj_value(mrb->exc), "inspect", 0);
    struct RString *error = mrb_str_ptr(obj);
    h2o_req_log_error(req, H2O_MRUBY_MODULE_NAME, "mruby raised: %s\n", error->as.heap.ptr);
    mrb->exc = NULL;
}

static void stringify_address(h2o_conn_t *conn, socklen_t (*cb)(h2o_conn_t *conn, struct sockaddr *), mrb_state *mrb,
                              mrb_value *host, mrb_value *port)
{
    struct sockaddr_storage ss;
    socklen_t sslen;
    char buf[NI_MAXHOST];

    *host = mrb_nil_value();
    *port = mrb_nil_value();

    if ((sslen = cb(conn, (void *)&ss)) == 0)
        return;
    size_t l = h2o_socket_getnumerichost((void *)&ss, sslen, buf);
    if (l != SIZE_MAX)
        *host = mrb_str_new(mrb, buf, l);
    int32_t p = h2o_socket_getport((void *)&ss);
    if (p != -1) {
        l = (int)sprintf(buf, "%" PRIu16, (uint16_t)p);
        *port = mrb_str_new(mrb, buf, l);
    }
}

static mrb_value build_env(h2o_req_t *req, mrb_state *mrb, mrb_value constants)
{
    mrb_value env = mrb_hash_new_capa(mrb, 16);
    mrb_int arena = mrb_gc_arena_save(mrb);

    /* environment */
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_REQUEST_METHOD), mrb_str_new(mrb, req->method.base, req->method.len));
    size_t confpath_len_wo_slash = req->pathconf->path.len - 1;
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_SCRIPT_NAME),
                 mrb_str_new(mrb, req->pathconf->path.base, confpath_len_wo_slash));
    mrb_hash_set(
        mrb, env, mrb_ary_entry(constants, LIT_PATH_INFO),
        mrb_str_new(mrb, req->path_normalized.base + confpath_len_wo_slash, req->path_normalized.len - confpath_len_wo_slash));
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_QUERY_STRING),
                 req->query_at != SIZE_MAX
                     ? mrb_str_new(mrb, req->path.base + req->query_at + 1, req->path.len - (req->query_at + 1))
                     : mrb_str_new_lit(mrb, ""));
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_SERVER_NAME),
                 mrb_str_new(mrb, req->hostconf->authority.host.base, req->hostconf->authority.host.len));
    {
        mrb_value h, p;
        stringify_address(req->conn, req->conn->get_sockname, mrb, &h, &p);
        if (!mrb_nil_p(h))
            mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_SERVER_ADDR), h);
        if (!mrb_nil_p(p))
            mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_SERVER_PORT), p);
    }
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, H2O_TOKEN_HOST - h2o__tokens),
                 mrb_str_new(mrb, req->authority.base, req->authority.len));
    if (req->entity.base != NULL) {
        char buf[32];
        int l = sprintf(buf, "%zu", req->entity.len);
        mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_CONTENT_LENGTH), mrb_str_new(mrb, buf, l));
    }
    {
        mrb_value h, p;
        stringify_address(req->conn, req->conn->get_peername, mrb, &h, &p);
        if (!mrb_nil_p(h))
            mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_REMOTE_ADDR), h);
        if (!mrb_nil_p(p))
            mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_REMOTE_PORT), p);
    }
    mrb_gc_arena_restore(mrb, arena);

    /* headers */
    size_t i = 0;
    for (i = 0; i != req->headers.size; ++i) {
        const h2o_header_t *header = req->headers.entries + i;
        mrb_value n;
        if (h2o_iovec_is_token(header->name)) {
            const h2o_token_t *token = H2O_STRUCT_FROM_MEMBER(h2o_token_t, buf, header->name);
            n = mrb_ary_entry(constants, (mrb_int)(token - h2o__tokens));
        } else {
            h2o_iovec_t vec = convert_header_name_to_env(&req->pool, header->name->base, header->name->len);
            n = mrb_str_new(mrb, vec.base, vec.len);
        }
        mrb_hash_set(mrb, env, n, mrb_str_new(mrb, header->value.base, header->value.len));
        mrb_gc_arena_restore(mrb, arena);
    }

    /* rack.* */
    /* TBD rack.version? */
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_RACK_URL_SCHEME),
                 mrb_str_new(mrb, req->scheme->name.base, req->scheme->name.len));
    /* we are using shared-none architecture, and therefore declare ourselves as multiprocess */
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_RACK_MULTITHREAD), mrb_false_value());
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_RACK_MULTIPROCESS), mrb_true_value());
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_RACK_RUN_ONCE), mrb_false_value());
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_RACK_HIJACK_), mrb_false_value());
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_RACK_ERRORS), mrb_gv_get(mrb, mrb_intern_lit(mrb, "$stderr")));

    /* server name */
    mrb_hash_set(mrb, env, mrb_ary_entry(constants, LIT_SERVER_SOFTWARE), mrb_ary_entry(constants, LIT_SERVER_SOFTWARE_VALUE));

    return env;
}

static int parse_rack_header(h2o_req_t *req, mrb_state *mrb, mrb_value name, mrb_value value)
{
    /* convert name to lowercase string */
    if (!mrb_string_p(name)) {
        name = mrb_str_to_str(mrb, name);
        if (mrb->exc != NULL) {
            report_exception(req, mrb);
            return -1;
        }
    }
    h2o_iovec_t lcname = h2o_strdup(&req->pool, RSTRING_PTR(name), RSTRING_LEN(name));
    h2o_strtolower(lcname.base, lcname.len);

    /* convert value to string */
    if (!mrb_string_p(value)) {
        value = mrb_str_to_str(mrb, value);
        if (mrb->exc != NULL) {
            report_exception(req, mrb);
            return -1;
        }
    }

    /* register the header, splitting the values with '\n' */
    const char *vstart = RSTRING_PTR(value), *vend = vstart + RSTRING_LEN(value), *eol;
    while (1) {
        for (eol = vstart; eol != vend; ++eol)
            if (*eol == '\n')
                break;
        if (h2o_memis(lcname.base, lcname.len, H2O_STRLIT("link")) &&
            h2o_register_push_path_in_link_header(req, vstart, eol - vstart)) {
            /* do not send the link header that is going to be pushed */
        } else {
            h2o_iovec_t vdup = h2o_strdup(&req->pool, vstart, eol - vstart);
            h2o_add_header_by_str(&req->pool, &req->res.headers, lcname.base, lcname.len, 1, vdup.base, vdup.len);
        }
        if (eol == vend)
            break;
        vstart = eol + 1;
    }

    return 0;
}

static int parse_rack_response(h2o_req_t *req, h2o_mruby_context_t *handler_ctx, mrb_value resp, h2o_iovec_t *content)
{
    mrb_state *mrb = handler_ctx->mrb;

    if (!mrb_array_p(resp)) {
        h2o_req_log_error(req, H2O_MRUBY_MODULE_NAME, "handler did not return an array");
        return -1;
    }

    { /* fetch status */
        mrb_value v = mrb_to_int(mrb, mrb_ary_entry(resp, 0));
        if (mrb->exc != NULL) {
            report_exception(req, mrb);
            return -1;
        }
        int status = mrb_fixnum(v);
        if (!(100 <= status && status <= 999)) {
            h2o_req_log_error(req, H2O_MRUBY_MODULE_NAME, "status returned by handler is out of range:%d\n", status);
            return -1;
        }
        req->res.status = status;
    }

    { /* fetch and set the headers */
        mrb_value headers = mrb_ary_entry(resp, 1);
        if (mrb_hash_p(headers)) {
            mrb_value keys = mrb_hash_keys(mrb, headers);
            mrb_int i, len = mrb_ary_len(mrb, keys);
            for (i = 0; i != len; ++i) {
                mrb_value k = mrb_ary_entry(keys, i);
                mrb_value v = mrb_hash_get(mrb, headers, k);
                if (parse_rack_header(req, mrb, k, v) != 0)
                    return -1;
            }
        } else {
            headers = mrb_funcall_argv(mrb, mrb_ary_entry(handler_ctx->constants, PROC_EACH_TO_ARRAY),
                                       handler_ctx->symbols.sym_call, 1, &headers);
            if (mrb->exc != NULL) {
                report_exception(req, mrb);
                return -1;
            }
            assert(mrb_array_p(headers));
            mrb_int i, len = mrb_ary_len(mrb, headers);
            for (i = 0; i != len; ++i) {
                mrb_value pair = mrb_ary_entry(headers, i);
                if (!mrb_array_p(pair)) {
                    h2o_req_log_error(req, H2O_MRUBY_MODULE_NAME, "headers#each did not return an array");
                    return -1;
                }
                if (parse_rack_header(req, mrb, mrb_ary_entry(pair, 0), mrb_ary_entry(pair, 1)) != 0)
                    return -1;
            }
        }
    }

    { /* convert response to string */
        mrb_value body = mrb_ary_entry(resp, 2);
        if (!mrb_array_p(body)) {
            /* convert to array by calling #each */
            mrb_value body_array = mrb_funcall_argv(mrb, mrb_ary_entry(handler_ctx->constants, PROC_EACH_TO_ARRAY),
                                                    handler_ctx->symbols.sym_call, 1, &body);
            if (mrb->exc != NULL) {
                report_exception(req, mrb);
                return -1;
            }
            assert(mrb_array_p(body_array));
            if (mrb_respond_to(mrb, body, handler_ctx->symbols.sym_close)) {
                mrb_funcall_argv(mrb, body, handler_ctx->symbols.sym_close, 0, NULL);
                if (mrb->exc != NULL) {
                    report_exception(req, mrb);
                    return -1;
                }
            }
            body = body_array;
        }
        mrb_int i, len = mrb_ary_len(mrb, body);
        /* calculate the length of the output, while at the same time converting the elements of the output array to string */
        content->len = 0;
        for (i = 0; i != len; ++i) {
            mrb_value e = mrb_ary_entry(body, i);
            if (!mrb_string_p(e)) {
                e = mrb_str_to_str(mrb, e);
                if (mrb->exc != NULL) {
                    report_exception(req, mrb);
                    return -1;
                }
                mrb_ary_set(mrb, body, i, e);
            }
            content->len += RSTRING_LEN(e);
        }
        /* allocate memory */
        char *dst = content->base = h2o_mem_alloc_pool(&req->pool, content->len);
        for (i = 0; i != len; ++i) {
            mrb_value e = mrb_ary_entry(body, i);
            assert(mrb_string_p(e));
            memcpy(dst, RSTRING_PTR(e), RSTRING_LEN(e));
            dst += RSTRING_LEN(e);
        }
    }

    return 0;
}

static int on_req(h2o_handler_t *_handler, h2o_req_t *req)
{
    h2o_mruby_handler_t *handler = (void *)_handler;
    h2o_mruby_context_t *handler_ctx = h2o_context_get_handler_context(req->conn->ctx, &handler->super);
    mrb_int arena = mrb_gc_arena_save(handler_ctx->mrb);
    h2o_iovec_t content;

    {
        /* call rack handler */
        mrb_value env = build_env(req, handler_ctx->mrb, handler_ctx->constants);
        mrb_value resp = mrb_funcall_argv(handler_ctx->mrb, handler_ctx->proc, handler_ctx->symbols.sym_call, 1, &env);
        if (handler_ctx->mrb->exc != NULL) {
            report_exception(req, handler_ctx->mrb);
            goto SendInternalError;
        }
        /* parse the resposne */
        if (parse_rack_response(req, handler_ctx, resp, &content) != 0)
            goto SendInternalError;
    }

    mrb_gc_arena_restore(handler_ctx->mrb, arena);

    /* fall through or send the response */
    if (req->res.status == STATUS_FALLTHRU)
        return -1;
    h2o_send_inline(req, content.base, content.len);
    return 0;

SendInternalError:
    mrb_gc_arena_restore(handler_ctx->mrb, arena);
    h2o_send_error(req, 500, "Internal Server Error", "Internal Server Error", 0);
    return 0;
}

h2o_mruby_handler_t *h2o_mruby_register(h2o_pathconf_t *pathconf, h2o_mruby_config_vars_t *vars)
{
    h2o_mruby_handler_t *handler = (void *)h2o_create_handler(pathconf, sizeof(*handler));

    handler->super.on_context_init = on_context_init;
    handler->super.on_context_dispose = on_context_dispose;
    handler->super.dispose = on_handler_dispose;
    handler->super.on_req = on_req;
    handler->config.source = h2o_strdup(NULL, vars->source.base, vars->source.len);
    if (vars->path != NULL)
        handler->config.path = h2o_strdup(NULL, vars->path, SIZE_MAX).base;

    return handler;
}
