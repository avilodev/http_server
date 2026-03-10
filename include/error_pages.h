#ifndef ERROR_PAGES_H
#define ERROR_PAGES_H

#include <stddef.h>

/**
 * Loads HTML error pages from public/error_pages/ into memory.
 * Call once at server startup.
 *
 * @param webroot Server webroot (e.g. SERVER_PATH)
 */
void error_pages_init(const char* webroot);

/**
 * Returns the in-memory content for the given HTTP status code, or NULL
 * if no custom page was loaded for that code.
 *
 * @param code    HTTP status code (e.g. 404)
 * @param out_len Set to the byte length of the returned content
 *
 * @return Pointer to null-terminated HTML string, or NULL
 */
const char* error_pages_get(int code, size_t* out_len);

/**
 * Frees all loaded error page content. Call at server shutdown.
 */
void error_pages_free(void);

#endif /* ERROR_PAGES_H */
