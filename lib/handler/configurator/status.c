/*
 * Copyright (c) 2016 DeNA Co., Ltd., Kazuho Oku
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
#include "h2o/configurator.h"

static int on_config_status(h2o_configurator_command_t *cmd, h2o_configurator_context_t *ctx, yoml_t *node)
{
    if (strcasecmp(node->data.scalar, "default") != 0) {
        h2o_configurator_errprintf(cmd, node, "unknown mode");
        return -1;
    }

    h2o_status_register(ctx->pathconf);
    return 0;
}

void h2o_status_register_configurator(h2o_globalconf_t *conf)
{
    h2o_configurator_t *c = h2o_configurator_create(conf, sizeof(*c));

    h2o_configurator_define_command(c, "status", H2O_CONFIGURATOR_FLAG_PATH | H2O_CONFIGURATOR_FLAG_DEFERRED |
                                                     H2O_CONFIGURATOR_FLAG_EXPECT_SCALAR,
                                    on_config_status);
}
