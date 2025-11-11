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

typedef void (*api_handler_t)(Client*);

typedef struct {
    const char* path;
    api_handler_t handler;
} ApiRoute;


void handle_api_request(Client* client);

void handle_api_status(Client* client); 
void handle_api_info(Client* client);
void handle_api_files(Client* client);
void handle_api_config(Client* client);
void handle_api_time(Client* client);

void send_api_error(Client* client, int status_code, const char* error_code, const char* message);


extern ApiRoute api_routes[];

#endif