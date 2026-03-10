#include "utils.h"
#include <ctype.h>

/**
 * Gets the current time added to by an offset. 
 *
 * @param offset Date offset in seconds. 
 *
 * @return Date specificed by current time and offset
 *
 * @warning Caller must free returned date
 */
char* get_time(int offset)
{
    time_t now = time(NULL);
    now += offset;
    struct tm tm;
    gmtime_r(&now, &tm);

    char* str = malloc(LARGE_ALLOCATE);
    strftime(str, LARGE_ALLOCATE, "%a, %d %b %Y %H:%M:%S GMT", &tm);

    return str;
}

void url_decode(char* dst, const char* src, size_t dst_size)
{
    if (!dst || !src || dst_size == 0) return;

    size_t out = 0;
    for (size_t i = 0; src[i] && out < dst_size - 1; i++) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && isxdigit((unsigned char)src[i+1])
                                 && isxdigit((unsigned char)src[i+2])) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[out++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

// Parse query parameter from URL, URL-decoding the value before returning.
// Example: /api/files?path=%2Fvideos returns "/videos" for key "path"
char* get_query_param(Client* client, const char* key)
{
    if (!client || !client->path || !key) {
        return NULL;
    }

    char* query_start = strchr(client->path, '?');
    if (!query_start) {
        return NULL;
    }

    query_start++;  // Skip '?'

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "%s=", key);

    char* param_start = strstr(query_start, search_key);
    if (!param_start) {
        return NULL;
    }

    param_start += strlen(search_key);

    /* Copy raw value then URL-decode it. */
    char raw[256];
    int i = 0;
    while (param_start[i] && param_start[i] != '&' && i < 255) {
        raw[i] = param_start[i];
        i++;
    }
    raw[i] = '\0';

    char* value = malloc(256);
    if (!value) return NULL;
    url_decode(value, raw, 256);
    return value;
}