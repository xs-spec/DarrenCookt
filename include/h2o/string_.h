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
#ifndef h2o__string_h
#define h2o__string_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "h2o/memory.h"

#define H2O_TO__STR(n) #n
#define H2O_TO_STR(n) H2O_TO__STR(n)

#define H2O_STRLIT(s) (s), sizeof(s) - 1

/* based on http://groups.google.com/group/comp.std.c/browse_thread/thread/77ee8c8f92e4a3fb/346fc464319b1ee5 */
#define H2O_NARG(...) H2O__NARG(__VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define H2O__NARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, N, ...) N

#define H2O_TIMESTR_RFC1123_LEN (sizeof("Sun, 06 Nov 1994 08:49:37 GMT") - 1)
#define H2O_TIMESTR_LOG_LEN (sizeof("29/Aug/2014:15:34:38 +0900") - 1)

/**
 * duplicates given string
 * @param pool memory pool (or NULL to use malloc)
 * @param s source string
 * @param len length of the source string (the result of strlen(s) used in case len is SIZE_MAX)
 * @return buffer pointing to the duplicated string (buf is NUL-terminated but the length does not include the NUL char)
 */
h2o_iovec_t h2o_strdup(h2o_mempool_t *pool, const char *s, size_t len);
/**
 * tr/A-Z/a-z/
 */
static int h2o_tolower(int ch);
/**
 * tests if target string (target_len bytes long) is equal to test string (test_len bytes long) after being converted to lower-case
 */
static int h2o_lcstris(const char *target, size_t target_len, const char *test, size_t test_len);
/**
 * parses a positive number of return SIZE_MAX if failed
 */
size_t h2o_strtosize(const char *s, size_t len);
/**
 * base64 url decoder
 */
h2o_iovec_t h2o_decode_base64url(h2o_mempool_t *pool, const char *src, size_t len);
/**
 * base64 encoder
 */
void h2o_base64_encode(char *dst, const void *src, size_t len, int url_encoded);
/**
 * builds a RFC-1123 style date string
 */
void h2o_time2str_rfc1123(char *buf, time_t time);
/**
 * builds an Apache log-style date string
 */
void h2o_time2str_log(char *buf, time_t time);
/**
 * returns the extension portion of path
 */
const char *h2o_get_filext(const char *path, size_t len);
/**
 * 
 */
const char *h2o_next_token(const char* elements, size_t elements_len, size_t *element_len, const char *cur);
/**
 * tests if string needle exists within a comma-separated string (for handling "#rule" of RFC 2616)
 */
int h2o_contains_token(const char *haysack, size_t haysack_len, const char *needle, size_t needle_len);
/**
 * removes "..", ".", decodes %xx from a path representation
 * @param pool memory pool to be used in case the path contained references to directories
 * @param path source path
 * @param len source length
 * @return buffer pointing to source, or buffer pointing to an allocated chunk with normalized representation of the given path
 */
h2o_iovec_t h2o_normalize_path(h2o_mempool_t *pool, const char *path, size_t len);
/**
 * parses absolute URL (either http or https)
 */
int h2o_parse_url(const char *url, size_t url_len, h2o_iovec_t *scheme, h2o_iovec_t *host, uint16_t *port, h2o_iovec_t *path);
/**
 * HTML-escapes a string
 * @param pool memory pool
 * @param src source string
 * @param len source length
 * @return the escaped string, or the source itself if escape was not necessary
 */
h2o_iovec_t h2o_htmlescape(h2o_mempool_t *pool, const char *src, size_t len);
/**
 * concatenates a list of iovecs (with NUL termination)
 */
#define h2o_concat(pool, ...) h2o__concat(pool, H2O_NARG(__VA_ARGS__), __VA_ARGS__)
h2o_iovec_t h2o__concat(h2o_mempool_t *pool, size_t n, ...);

int h2o__lcstris_core(const char *target, const char *test, size_t test_len);

/* inline defs */

inline int h2o_tolower(int ch)
{
    return 'A' <= ch && ch <= 'Z' ? ch + 0x20 : ch;
}

inline int h2o_lcstris(const char *target, size_t target_len, const char *test, size_t test_len)
{
    if (target_len != test_len)
        return 0;
    return h2o__lcstris_core(target, test, test_len);
}

#ifdef __cplusplus
}
#endif

#endif
