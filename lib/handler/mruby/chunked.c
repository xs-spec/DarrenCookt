/*
 * Copyright (c) 2015 DeNA Co., Ltd., Kazuho Oku
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
#include <stdlib.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <mruby/string.h>
#include "h2o/mruby_.h"

struct st_h2o_mruby_chunked_t {
    h2o_buffer_t *receiving;
    h2o_doublebuffer_t sending;
    int eos_received;
};

static void do_send(h2o_mruby_generator_t *generator)
{
    h2o_mruby_chunked_t *chunked = generator->chunked;

    assert(chunked->sending.bytes_inflight == 0);

    h2o_iovec_t buf = h2o_doublebuffer_prepare(&chunked->sending, &chunked->receiving, generator->req->preferred_chunk_size);
    size_t bufcnt = 1;
    int is_eos = 0;

    if (chunked->eos_received && buf.len == chunked->sending.buf->size && chunked->receiving->size == 0) {
        if (buf.len == 0)
            --bufcnt;
        is_eos = 1;
    } else {
        if (buf.len == 0)
            return;
    }

    h2o_send(generator->req, &buf, bufcnt, is_eos);
}

static void do_proceed(h2o_generator_t *_generator, h2o_req_t *req)
{
    h2o_mruby_generator_t *generator = (void *)_generator;

    h2o_doublebuffer_consume(&generator->chunked->sending);
    do_send(generator);
}

mrb_value h2o_mruby_send_chunked_init(h2o_mruby_generator_t *generator)
{
    h2o_mruby_chunked_t *chunked = h2o_mem_alloc_pool(&generator->req->pool, sizeof(*chunked));
    h2o_buffer_init(&chunked->receiving, &h2o_socket_buffer_prototype);
    h2o_doublebuffer_init(&chunked->sending, &h2o_socket_buffer_prototype);
    chunked->eos_received = 0;

    generator->super.proceed = do_proceed;
    generator->chunked = chunked;
    return mrb_ary_entry(generator->ctx->constants, H2O_MRUBY_CHUNKED_PROC_EACH_TO_FIBER);
}

void h2o_mruby_send_chunked_dispose(h2o_mruby_generator_t *generator)
{
    h2o_mruby_chunked_t *chunked = generator->chunked;

    h2o_buffer_dispose(&chunked->receiving);
    h2o_doublebuffer_dispose(&chunked->sending);
}

static mrb_value check_precond(mrb_state *mrb, h2o_mruby_generator_t *generator)
{
    if (generator == NULL || generator->req == NULL)
        return mrb_exc_new_str_lit(mrb, E_RUNTIME_ERROR, "downstream HTTP closed");
    if (generator->req->_generator == NULL)
        return mrb_exc_new_str_lit(mrb, E_RUNTIME_ERROR, "cannot send chunk before sending headers");
    return mrb_nil_value();
}

static mrb_value send_chunked_method(mrb_state *mrb, mrb_value self)
{
    h2o_mruby_generator_t *generator = h2o_mruby_current_generator;
    const char *s;
    mrb_int len;

    /* parse args */
    mrb_get_args(mrb, "s", &s, &len);

    { /* precond check */
        mrb_value exc = check_precond(mrb, generator);
        if (!mrb_nil_p(exc))
            mrb_exc_raise(mrb, exc);
    }

    /* append to send buffer, and send out immediately if necessary */
    if (len != 0) {
        h2o_mruby_chunked_t *chunked = generator->chunked;
        h2o_buffer_reserve(&chunked->receiving, len);
        memcpy(chunked->receiving->bytes + chunked->receiving->size, s, len);
        chunked->receiving->size += len;
        if (chunked->sending.bytes_inflight == 0)
            do_send(generator);
    }

    return mrb_nil_value();
}

mrb_value h2o_mruby_send_chunked_eos_callback(h2o_mruby_generator_t *generator, mrb_value receiver, mrb_value input,
                                              int *next_action)
{
    mrb_state *mrb = generator->ctx->mrb;

    { /* precond check */
        mrb_value exc = check_precond(mrb, generator);
        if (!mrb_nil_p(exc))
            return exc;
    }

    h2o_mruby_send_chunked_close(generator);

    *next_action = H2O_MRUBY_CALLBACK_NEXT_ACTION_STOP;
    return mrb_nil_value();
}

void h2o_mruby_send_chunked_close(h2o_mruby_generator_t *generator)
{
    h2o_mruby_chunked_t *chunked = generator->chunked;

    chunked->eos_received = 1;
    if (chunked->sending.bytes_inflight == 0)
        do_send(generator);
}

void h2o_mruby_send_chunked_init_context(h2o_mruby_context_t *ctx)
{
    mrb_state *mrb = ctx->mrb;

    mrb_define_method(mrb, mrb->kernel_module, "_h2o_send_chunk", send_chunked_method, MRB_ARGS_ARG(1, 0));
    h2o_mruby_define_callback(mrb, "_h2o_send_chunk_eos", H2O_MRUBY_CALLBACK_ID_SEND_CHUNKED_EOS);
    mrb_ary_set(mrb, ctx->constants, H2O_MRUBY_CHUNKED_PROC_EACH_TO_FIBER,
                h2o_mruby_eval_expr(mrb, "Proc.new do |src|\n"
                                         "  fiber = Fiber.new do\n"
                                         "    src.each do |chunk|\n"
                                         "      _h2o_send_chunk(chunk)\n"
                                         "    end\n"
                                         "    _h2o_send_chunk_eos()\n"
                                         "  end\n"
                                         "  fiber.resume\n"
                                         "end"));
    h2o_mruby_assert(mrb);
}
