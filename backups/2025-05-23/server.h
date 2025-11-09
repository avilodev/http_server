#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "node.h"
 
#define HTTP_PORT 80
#define HTTPS_PORT 443

#define BACKLOG 20
#define MAXLINE 4096

#define MAX_RESPONSE 32300
#define MAX_CLIENTS 50

#define IP_REDIRECT "100.64.18.186"

//Milestones
/*
Snap/0.1 - Simple Server
Snap/0.2 - Added Caching (304) 
*/

#define SERVER "Snap/0.2"

extern volatile sig_atomic_t SIGNAL_FLAG;
void signal_handler(int signum);
void updateTree(int signum);

struct Client {
    char* client_ip;
    int client_port;
    
    int client_fd;
    int fd;
    char* full_path;
    
    char* method;
    char* path;
    char* version;
    
    char* host;
    unsigned int tag;
    int connection_status;
    
    int DNT;
    int GPC;
    
    char* user_agent;
    
    char* referer;
    int upgrade_tls;
    
    char* accept;
    char* encoding;
    char* language;
    
    char* priority;
    char* request;

    int is_ssl;
    SSL* ssl;
};

struct Client* init_request(char*, int, bool);
int process_request(struct Client*, struct Node*);
int send_response(struct Node*, struct Client*);
char* get_time(int);
char* content_type(char*);
int master_log(int, struct Client*);
int send_code(int, int);

#endif 