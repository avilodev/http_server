#ifndef TYPES_H
#define TYPES_H

#include <sys/types.h>
#include <netinet/in.h>
#include <openssl/ssl.h>

// Server constants
#define HTTP_PORT 80
#define HTTPS_PORT 443
#define BACKLOG 20
#define SERVER_PATH "/home/remote/server"
#define MAX_REQUEST_SIZE 8192
#define MAX_RESPONSE_SIZE 262144
#define SMALL_ALLOCATE 256
#define LARGE_ALLOCATE 16384

// Forward declarations
struct Node;

// Client request structure
typedef struct Client {
    // Connection info
    char* client_ip;
    int client_port;
    int client_fd;
    
    // File handling
    int fd;                  // File descriptor for requested file
    char* full_path;         // Full path to file
    
    // HTTP request line
    char* method;            // GET, HEAD, POST, etc.
    char* path;              // Requested path
    char* version;           // HTTP/1.0 or HTTP/1.1
    
    // HTTP headers
    char* host;
    char* user_agent;
    char* referer;
    char* accept;
    char* encoding;
    char* language;
    char* priority;
    char* modified_since;    // If-Modified-Since
    
    // Caching
    unsigned int tag;        // ETag value
    
    // Connection management
    int connection_status;   // 0=close, 1=keep-alive
    
    // Range requests
    int range;               // 0=no range, 1=range request
    off_t start_range;
    off_t end_range;
    
    // Privacy flags
    int DNT;                 // Do Not Track
    int GPC;                 // Global Privacy Control
    int upgrade_tls;         // Upgrade-Insecure-Requests
    
    // SSL
    int is_ssl;
    SSL* ssl;
} Client;

// Thread arguments for worker threads
typedef struct ThreadArgs {
    int client_fd;
    SSL* ssl;
    struct sockaddr_in client_addr;
    struct Node* tree_head;  // Cache tree
} ThreadArgs;

// Server configuration
typedef struct ServerConfig {
    char* webroot;
    int http_port;
    int https_port;
    char* cert_path;
    char* key_path;
    int thread_pool_size;
    int max_queue_size;
} ServerConfig;

#endif // TYPES_H
