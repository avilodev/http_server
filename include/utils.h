#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>

#include "types.h"

char* get_time(int offset);
char* get_query_param(Client* client, const char* key);

/**
 * Decodes a percent-encoded URL string in-place.
 * Converts %XX hex sequences and '+' (to space).
 * Safe to call with src == dst (in-place decode).
 *
 * @param dst Output buffer (may equal src for in-place)
 * @param src Input string
 * @param dst_size Size of dst buffer
 */
void url_decode(char* dst, const char* src, size_t dst_size);

#endif 