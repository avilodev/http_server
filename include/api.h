#ifndef API_H
#define API_H

#include "types.h"
#include "response.h"
#include "config.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>  
#include <dirent.h>       
#include <sys/stat.h>    
#include <unistd.h>

typedef void (*api_handler_t)(client_t*);

typedef struct {
	const char* path;
	api_handler_t handler;
} api_route;

void handle_api_request(client_t* client);

void handle_api_status(client_t* client);
void handle_api_info(client_t* client);
void handle_api_files(client_t* client);
void handle_api_config(client_t* client);
void handle_api_time(client_t* client);
void handle_api_logout(client_t* client);

void send_api_error(client_t* client, int status_code, const char* error_code, const char* message);

extern api_route api_routes[];

#endif /* API_H */