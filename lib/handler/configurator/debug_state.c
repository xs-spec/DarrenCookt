/*
 * Copyright (c) 2016 DeNA Co., Ltd., Ichito Nagata
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

static int on_config_debug_state(h2o_configurator_command_t *cmd, h2o_configurator_context_t *ctx, yoml_t *node)
{
    switch (node->type) {
    case YOML_TYPE_SCALAR:
        switch (h2o_configurator_get_one_of(cmd, node, "OFF,ON")) {
        case 0: /* OFF */
            return 0;
        case 1: /* ON */
            h2o_debug_state_register(ctx->hostconf, 0);
            return 0;
        default: /* error */
            return -1;
        }
        break;
    case YOML_TYPE_MAPPING: {
        int hpack_enabled = 0;
        yoml_t *t;
        if ((t = yoml_get(node, "hpack")) != NULL) {
            if (t->type != YOML_TYPE_SCALAR) {
                h2o_configurator_errprintf(cmd, t, "`hpack` must be scalar");
                return -1;
            }
            hpack_enabled = (int)h2o_configurator_get_one_of(cmd, t, "OFF,ON");
            if (! (hpack_enabled == 0 || hpack_enabled == 1)) {
                h2o_configurator_errprintf(cmd, t, "`hpack` must be either of `OFF`, `ON`");
                return -1;
            }
        }
        h2o_debug_state_register(ctx->hostconf, hpack_enabled);
        return 0;
    } break;
    default:
        h2o_configurator_errprintf(cmd, node, "node must be a scalar or a mapping");
        return -1;
    }

}

void h2o_debug_state_register_configurator(h2o_globalconf_t *conf)
{
    struct st_h2o_configurator_t *c = (void *)h2o_configurator_create(conf, sizeof(*c));

    h2o_configurator_define_command(c, "debug-state", H2O_CONFIGURATOR_FLAG_HOST | H2O_CONFIGURATOR_FLAG_DEFERRED, on_config_debug_state);
}
