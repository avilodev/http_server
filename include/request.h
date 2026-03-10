#ifndef REQUEST_H
#define REQUEST_H

#include "types.h"

// Main request parsing function
// Returns NULL on error (sends error response internally)
Client* parse_http_request(char* raw_request, int client_fd, SSL* ssl);

// Request validation
int validate_http_method(const char* method);
int validate_http_version(const char* version);
int validate_path(const char* path);

// Path resolution
char* resolve_request_path(const char* request_path, const char* webroot);

// Request cleanup
void free_client(Client* client);

// Helper: print request for debugging
void print_client_info(const Client* client);

#endif // REQUEST_H