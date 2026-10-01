#ifndef REQUEST_H
#define REQUEST_H

#include "types.h"

// Main request parsing function Returns NULL on error
client_t* parse_http_request(char* raw_request, int client_fd, SSL* ssl);

// Request validation
int validate_http_method(const char* method);
int validate_http_version(const char* version);
int validate_path(const char* path);

// Path resolution
char* resolve_request_path(const char* request_path, const char* webroot);

// Request cleanup
void free_client(client_t* client);

// Helper: print request for debugging
void print_client_info(const client_t* client);

#endif /* REQUEST_H */