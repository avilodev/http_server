#ifndef RESPONSE_H
#define RESPONSE_H

#include "types.h"
#include "mime.h"

// Forward declaration
struct node_t;

// Main response functions
int send_file_response(client_t* client, struct node_t* cache_node);
int send_error_response(int status_code, client_t* client);
int send_not_modified_response(client_t* client, struct node_t* cache_node);
int send_redirect_response(const char* location, client_t* client);
int send_login_redirect(const char* location, const char* token, int max_age, client_t* client);
int send_options_response(client_t* client);

// Status code helpers
const char* get_status_message(int code);

// Date/time formatting
char* format_http_date(time_t timestamp);
char* get_current_http_date(void);

void send_api_response(client_t* client, int code, char* mime_type, char* body);

#endif /* RESPONSE_H */