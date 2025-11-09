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

#define PORT 80
#define MAXLINE 4096
#define MAX_RESPONSE 32300
#define MAX_CLIENTS 10

struct Client* init_request(char*, int);
int process_request(struct Client*);
int send_response(struct Client*);
char* get_time(int);
char* content_type(char*);
int send_error(int, int);

int SIGNAL_FLAG = 0;

struct Client
{
    int client_fd;
    int fd;
    char* full_path;

    char* method;
    char* path;
    char* version;

    char* host;
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
};
