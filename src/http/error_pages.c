#include "error_pages.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Codes we attempt to load from disk. Extend this list to add more pages. */
static const int KNOWN_CODES[] = { 400, 403, 404, 500 };
#define NUM_KNOWN_CODES ((int)(sizeof(KNOWN_CODES) / sizeof(KNOWN_CODES[0])))

typedef struct {
    int    code;
    char*  content;
    size_t len;
} ErrorPage;

static ErrorPage g_pages[NUM_KNOWN_CODES];
static int       g_page_count = 0;

void error_pages_init(const char* webroot)
{
    g_page_count = 0;

    for (int i = 0; i < NUM_KNOWN_CODES; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/public/error_pages/%d.html",
                 webroot, KNOWN_CODES[i]);

        FILE* f = fopen(path, "rb");
        if (!f) continue;

        struct stat st;
        if (fstat(fileno(f), &st) < 0 || st.st_size == 0) {
            fclose(f);
            continue;
        }

        char* buf = malloc((size_t)st.st_size + 1);
        if (!buf) { fclose(f); continue; }

        size_t n = fread(buf, 1, (size_t)st.st_size, f);
        fclose(f);

        buf[n] = '\0';

        g_pages[g_page_count].code    = KNOWN_CODES[i];
        g_pages[g_page_count].content = buf;
        g_pages[g_page_count].len     = n;
        g_page_count++;

        log_message(LOG_INFO, "Loaded error page %d (%zu bytes)", KNOWN_CODES[i], n);
    }
}

const char* error_pages_get(int code, size_t* out_len)
{
    for (int i = 0; i < g_page_count; i++) {
        if (g_pages[i].code == code) {
            if (out_len) *out_len = g_pages[i].len;
            return g_pages[i].content;
        }
    }
    return NULL;
}

void error_pages_free(void)
{
    for (int i = 0; i < g_page_count; i++) {
        free(g_pages[i].content);
        g_pages[i].content = NULL;
    }
    g_page_count = 0;
}
