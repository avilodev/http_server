#include "session.h"

#include <sodium.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

typedef struct Session {
    char token[SESSION_TOKEN_LEN + 1];
    char username[256];
    time_t expires;
    struct Session* next;
} Session;

static Session*        g_sessions      = NULL;
static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;

static void generate_token(char* out) {
    unsigned char raw[32];
    randombytes_buf(raw, sizeof(raw));
    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", raw[i]);
    }
    out[SESSION_TOKEN_LEN] = '\0';
}

char* session_create(const char* username) {
    if (!username) return NULL;

    Session* s = calloc(1, sizeof(Session));
    if (!s) return NULL;

    generate_token(s->token);
    strncpy(s->username, username, sizeof(s->username) - 1);
    s->expires = time(NULL) + SESSION_EXPIRY_SEC;

    pthread_mutex_lock(&g_session_mutex);
    s->next    = g_sessions;
    g_sessions = s;
    pthread_mutex_unlock(&g_session_mutex);

    return strdup(s->token);
}

const char* session_get_user(const char* token) {
    if (!token) return NULL;

    pthread_mutex_lock(&g_session_mutex);
    time_t now = time(NULL);
    const char* result = NULL;
    Session* s = g_sessions;
    while (s) {
        if (strncmp(s->token, token, SESSION_TOKEN_LEN) == 0) {
            if (s->expires > now) result = s->username;
            break;
        }
        s = s->next;
    }
    pthread_mutex_unlock(&g_session_mutex);
    return result;
}

void session_destroy(const char* token) {
    if (!token) return;

    pthread_mutex_lock(&g_session_mutex);
    Session** prev = &g_sessions;
    Session*  s    = g_sessions;
    while (s) {
        if (strncmp(s->token, token, SESSION_TOKEN_LEN) == 0) {
            *prev = s->next;
            free(s);
            break;
        }
        prev = &s->next;
        s    = s->next;
    }
    pthread_mutex_unlock(&g_session_mutex);
}

void session_cleanup_expired(void) {
    pthread_mutex_lock(&g_session_mutex);
    time_t    now  = time(NULL);
    Session** prev = &g_sessions;
    Session*  s    = g_sessions;
    while (s) {
        if (s->expires <= now) {
            Session* next = s->next;
            *prev = next;
            free(s);
            s = next;
        } else {
            prev = &s->next;
            s    = s->next;
        }
    }
    pthread_mutex_unlock(&g_session_mutex);
}

void session_store_destroy(void) {
    pthread_mutex_lock(&g_session_mutex);
    Session* s = g_sessions;
    while (s) {
        Session* next = s->next;
        free(s);
        s = next;
    }
    g_sessions = NULL;
    pthread_mutex_unlock(&g_session_mutex);
    pthread_mutex_destroy(&g_session_mutex);
}
