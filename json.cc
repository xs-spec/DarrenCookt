#include "json.h"
#include <cinttypes>

static bool json_need_escape(char c)
{
    return static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
}

static void json_write_str_value(FILE *out, const char *str)
{
    fputc('"', out);
    while (*str) {
        switch (*str) {
        case '\"':
            fprintf(out, "\\\"");
            break;
        case '\\':
            fprintf(out, "\\\\");
            break;
        case '\b':
            fprintf(out, "\\b");
            break;
        case '\f':
            fprintf(out, "\\f");
            break;
        case '\n':
            fprintf(out, "\\n");
            break;
        case '\r':
            fprintf(out, "\\r");
            break;
        case '\t':
            fprintf(out, "\\t");
            break;
        default:
            if (!json_need_escape(*str)) {
                fputc(*str, out);
            } else {
                auto u8 = static_cast<uint8_t>(*str);
                fprintf(out, "\\u%04x", u8);
            }
            break;
        }
        str++;
    }
    fputc('"', out);
}

static void json_write_name_value(FILE *out, const char *name)
{
    fprintf(out, "\"%s\":", name);
}

void json_write_pair(FILE *out, bool comma, const char *name, const char *value)
{
    if (comma) {
        fputc(',', out);
    }
    json_write_name_value(out, name);
    json_write_str_value(out, value);
}

void json_write_pair(FILE *out, bool comma, const char *name, const void *value, size_t len)
{
    if (comma) {
        fputc(',', out);
    }
    json_write_name_value(out, name);
    fputc('"', out);
    const uint8_t *bin = static_cast<const uint8_t *>(value);
    for (size_t i = 0; i < len; i++) {
        fputc("0123456789abcdef"[bin[i] >> 4], out);
        fputc("0123456789abcdef"[bin[i] & 0xf], out);
    }
    fputc('"', out);
}

void json_write_pair(FILE *out, bool comma, const char *name, const int32_t value)
{
    json_write_pair(out, comma, name, (int64_t)value);
}

void json_write_pair(FILE *out, bool comma, const char *name, const uint32_t value)
{
    json_write_pair(out, comma, name, (uint64_t)value);
}

void json_write_pair(FILE *out, bool comma, const char *name, const int64_t value)
{
    if (comma) {
        fputc(',', out);
    }
    json_write_name_value(out, name);
    fprintf(out, "%" PRId64, value);
}

void json_write_pair(FILE *out, bool comma, const char *name, const uint64_t value)
{
    if (comma) {
        fputc(',', out);
    }
    json_write_name_value(out, name);
    fprintf(out, "%" PRIu64, value);
}
