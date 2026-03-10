#ifndef SESSION_H
#define SESSION_H

#define SESSION_TOKEN_LEN  64    // 32 random bytes hex-encoded → 64 printable chars
#define SESSION_EXPIRY_SEC 1800  // 30-minute session lifetime

// Creates a new session for username. Returns a malloc'd copy of the token.
// Caller must free() the returned string.
char* session_create(const char* username);

// Returns the username for a valid, non-expired session token, or NULL.
// Returned pointer is valid only while the session mutex is NOT held by the caller.
const char* session_get_user(const char* token);

// Removes the session identified by token.
void session_destroy(const char* token);

// Removes all expired sessions (call periodically if desired).
void session_cleanup_expired(void);

// Frees the entire session store. Call once at server shutdown.
void session_store_destroy(void);

#endif // SESSION_H
