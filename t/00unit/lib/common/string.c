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
#include "../../test.h"
#include "../../../../lib/common/string.c"

static void test_stripws(void)
{
    h2o_iovec_t t;

    t = h2o_str_stripws(H2O_STRLIT(""));
    ok(h2o_memis(t.base, t.len, H2O_STRLIT("")));
    t = h2o_str_stripws(H2O_STRLIT("hello world"));
    ok(h2o_memis(t.base, t.len, H2O_STRLIT("hello world")));
    t = h2o_str_stripws(H2O_STRLIT("   hello world"));
    ok(h2o_memis(t.base, t.len, H2O_STRLIT("hello world")));
    t = h2o_str_stripws(H2O_STRLIT("hello world   "));
    ok(h2o_memis(t.base, t.len, H2O_STRLIT("hello world")));
    t = h2o_str_stripws(H2O_STRLIT("   hello world   "));
    ok(h2o_memis(t.base, t.len, H2O_STRLIT("hello world")));
    t = h2o_str_stripws(H2O_STRLIT("     "));
    ok(h2o_memis(t.base, t.len, H2O_STRLIT("")));
}

static void test_next_token(void)
{
    h2o_iovec_t iter;
    const char *token;
    size_t token_len;

#define NEXT()                                                                                                                     \
    if ((token = h2o_next_token(&iter, ',', &token_len, NULL)) == NULL) {                                                          \
        ok(0);                                                                                                                     \
        return;                                                                                                                    \
    }

    iter = h2o_iovec_init(H2O_STRLIT("public, max-age=86400, must-revalidate"));
    NEXT();
    ok(h2o_memis(token, token_len, H2O_STRLIT("public")));
    NEXT();
    ok(h2o_memis(token, token_len, H2O_STRLIT("max-age=86400")));
    NEXT();
    ok(h2o_memis(token, token_len, H2O_STRLIT("must-revalidate")));
    token = h2o_next_token(&iter, ',', &token_len, NULL);
    ok(token == NULL);

    iter = h2o_iovec_init(H2O_STRLIT("  public  ,max-age=86400  ,"));
    NEXT();
    ok(h2o_memis(token, token_len, H2O_STRLIT("public")));
    NEXT();
    ok(h2o_memis(token, token_len, H2O_STRLIT("max-age=86400")));
    token = h2o_next_token(&iter, ',', &token_len, NULL);
    ok(token == NULL);

    iter = h2o_iovec_init(H2O_STRLIT(""));
    token = h2o_next_token(&iter, ',', &token_len, NULL);
    ok(token == NULL);

    iter = h2o_iovec_init(H2O_STRLIT(", ,a, "));
    NEXT();
    ok(token_len == 0);
    NEXT();
    ok(token_len == 0);
    NEXT();
    ok(h2o_memis(token, token_len, H2O_STRLIT("a")));
    token = h2o_next_token(&iter, ',', &token_len, NULL);
    ok(token == NULL);

#undef NEXT
}

static void test_next_token2(void)
{
    h2o_iovec_t iter, value;
    const char *name;
    size_t name_len;

#define NEXT()                                                                                                                     \
    if ((name = h2o_next_token(&iter, ',', &name_len, &value)) == NULL) {                                                          \
        ok(0);                                                                                                                     \
        return;                                                                                                                    \
    }

    iter = h2o_iovec_init(H2O_STRLIT("public, max-age=86400, must-revalidate"));
    NEXT();
    ok(h2o_memis(name, name_len, H2O_STRLIT("public")));
    ok(value.base == NULL);
    ok(value.len == 0);
    NEXT();
    ok(h2o_memis(name, name_len, H2O_STRLIT("max-age")));
    ok(h2o_memis(value.base, value.len, H2O_STRLIT("86400")));
    NEXT();
    ok(h2o_memis(name, name_len, H2O_STRLIT("must-revalidate")));
    ok(value.base == NULL);
    ok(value.len == 0);
    name = h2o_next_token(&iter, ',', &name_len, &value);
    ok(name == NULL);

    iter = h2o_iovec_init(H2O_STRLIT("public, max-age = 86400 = c , must-revalidate="));
    NEXT();
    ok(h2o_memis(name, name_len, H2O_STRLIT("public")));
    ok(value.base == NULL);
    ok(value.len == 0);
    NEXT();
    ok(h2o_memis(name, name_len, H2O_STRLIT("max-age")));
    ok(h2o_memis(value.base, value.len, H2O_STRLIT("86400 = c")));
    NEXT();
    ok(h2o_memis(name, name_len, H2O_STRLIT("must-revalidate")));
    name = h2o_next_token(&iter, ',', &name_len, &value);
    ok(h2o_memis(value.base, value.len, H2O_STRLIT("")));

#undef NEXT
}

static void test_decode_base64(void)
{
    h2o_mem_pool_t pool;
    char buf[256];

    h2o_mem_init_pool(&pool);

    h2o_iovec_t src = {H2O_STRLIT("The quick brown fox jumps over the lazy dog.")}, decoded;
    h2o_base64_encode(buf, (const uint8_t *)src.base, src.len, 1);
    ok(strcmp(buf, "VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZy4") == 0);
    decoded = h2o_decode_base64url(&pool, buf, strlen(buf));
    ok(src.len == decoded.len);
    ok(strcmp(decoded.base, src.base) == 0);

    h2o_mem_clear_pool(&pool);
}

static void test_htmlescape(void)
{
    h2o_mem_pool_t pool;
    h2o_mem_init_pool(&pool);

#define TEST(src, expected)                                                                                                        \
    do {                                                                                                                           \
        h2o_iovec_t escaped = h2o_htmlescape(&pool, H2O_STRLIT(src));                                                              \
        ok(h2o_memis(escaped.base, escaped.len, H2O_STRLIT(expected)));                                                            \
    } while (0)

    TEST("hello world", "hello world");
    TEST("x < y", "x &lt; y");
    TEST("\0\"&'<>", "\0&quot;&amp;&#39;&lt;&gt;");

#undef TEST

    h2o_mem_clear_pool(&pool);
}

void test_lib__string_c(void)
{
    subtest("stripws", test_stripws);
    subtest("next_token", test_next_token);
    subtest("next_token2", test_next_token2);
    subtest("decode_base64", test_decode_base64);
    subtest("htmlescape", test_htmlescape);
}
