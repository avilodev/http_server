#ifndef API_H
#define API_H

#include "types.h"
#include "response.h"

typedef void (*api_handler_t)(Client*);

typedef struct {
    const char* path;
    api_handler_t handler;
} ApiRoute;


void handle_api_request(Client* client);

void handle_api_status(Client* client);
void handle_api_files(Client* client);
void handle_api_config(Client* client);

extern ApiRoute api_routes[];

#endif