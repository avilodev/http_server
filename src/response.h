#ifndef RESPONSE_H
#define RESPONSE_H

#include "types.h"

// Forward declaration
struct Node;

// Main response functions
int send_file_response(Client* client, struct Node* cache_node);
int send_error_response(int status_code, Client* client);
int send_not_modified_response(Client* client, struct Node* cache_node);
int send_redirect_response(const char* location, Client* client);
int send_options_response(Client* client);

// Status code helpers
const char* get_status_message(int code);

// Content type detection
char* get_content_type(const char* path);

// Date/time formatting
char* format_http_date(time_t timestamp);
char* get_current_http_date(void);

#endif // RESPONSE_H