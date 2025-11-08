#include "types.h"
#include "request.h"
#include "response.h"
#include "thread_pool.h"
#include "logger.h"
#include "ssl_handler.h"
#include "cache.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/err.h>

// Global variables for signal handling
static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_refresh_cache = 0;

// Global thread pool
static struct ThreadPool* g_thread_pool = NULL;


/**
 * Signal handler for managing server and cache operations.
 * 
 * Handles system signals to control server shutdown and cache refresh.
 * This function is async-signal-safe and only modifies sig_atomic_t variables.
 * 
 * @param signum The signal number that triggered this handler
 * 
 * @note SIGINT/SIGTERM/SIGQUIT trigger graceful shutdown
 * @note SIGUSR1 triggers cache tree refresh
 * @warning This function runs in signal context - keep it minimal
 */
void signal_handler(int signum) {
    switch (signum) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            printf("\nReceived shutdown signal (%d)\n", signum);
            g_shutdown = 1;
            break;
        case SIGUSR1:
            printf("Received cache refresh signal\n");
            g_refresh_cache = 1;
            break;
        default:
            break;
    }
}

/**
 * Setup signal handlers for server management.
 * 
 * Configures handlers for shutdown signals (SIGINT/SIGTERM/SIGQUIT),
 * cache refresh signal (SIGUSR1), and ignores SIGPIPE.
 * 
 * @note SIGPIPE is ignored to prevent crashes on broken connections
 * @note All other signals invoke signal_handler()
 */
void setup_signals(void) {
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
}

/**
 * Main worker thread for every new client
 *
 * Read and logs client request, checks for HTTP upgrades, 
 *  validates asking path, checks for cached responses (304), 
 *  then sends file. All resources are cleaned up before thread exit.
 *
 * @param arg ThreadArgs* object - holds client info
 *
 * @return Always returns NULL (pthread requirement)
 *
 * @note This function is called by thread pool workers
 * @note All allocated resources (Client, SSL, ThreadArgs) are freed internally
 * @warning Do not call directly - submit via threadpool_add_work()
 *
 * @see threadpool_add_work(), parse_http_request(), send_file_response()
 */
void* handle_client_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    
    char request_buffer[8192];
    memset(request_buffer, 0, sizeof(request_buffer));
    
    // Read request from client
    ssize_t recv_len;
    if (args->ssl) {
        recv_len = SSL_read(args->ssl, request_buffer, sizeof(request_buffer) - 1);
    } else {
        recv_len = recv(args->client_fd, request_buffer, sizeof(request_buffer) - 1, 0);
    }
    
    if (recv_len <= 0) {
        log_message(LOG_WARN, "Failed to read request or client disconnected");
        goto cleanup;
    }
    
    request_buffer[recv_len] = '\0';
    log_message(LOG_DEBUG, "Received %ld bytes from client", recv_len);
    
    // Parse HTTP request
    Client* client = parse_http_request(request_buffer, args->client_fd, args->ssl);
    if (!client) {
        log_message(LOG_ERROR, "Failed to parse request");
        goto cleanup;
    }
    
    // Set client connection info
    client->client_ip = inet_ntoa(args->client_addr.sin_addr);
    client->client_port = ntohs(args->client_addr.sin_port);
    
    log_message(LOG_INFO, "Request from %s:%d - %s %s %s", 
                client->client_ip, client->client_port,
                client->method, client->path, client->version);
    
    // Print detailed client info (debug)
    print_client_info(client);
    
    // Handle TLS upgrade redirect (HTTP only)
    if (!args->ssl && client->upgrade_tls) {
        char redirect_url[512];
        snprintf(redirect_url, sizeof(redirect_url), "https://%s%s", 
                 client->host ? client->host : "localhost", client->path);
        
        log_message(LOG_INFO, "Redirecting to HTTPS: %s", redirect_url);
        send_redirect_response(redirect_url, client);
        free_client(client);
        goto cleanup;
    }
    
    // Validate HTTP method
    if (!validate_http_method(client->method)) {
        if (strcmp(client->method, "OPTIONS") == 0) {
            log_message(LOG_INFO, "Handling OPTIONS request");
            send_options_response(client);
        } else {
            log_message(LOG_WARN, "Unsupported method: %s", client->method);
            send_error_response(501, client);
        }
        free_client(client);
        goto cleanup;
    }
    
    // Validate path for security
    if (!validate_path(client->path)) {
        log_message(LOG_WARN, "Invalid/dangerous path detected: %s", client->path);
        send_error_response(403, client);
        free_client(client);
        goto cleanup;
    }
    
    // Resolve full filesystem path
    extern struct ServerConfig g_config;  // From config.c
    client->full_path = resolve_request_path(client->path, g_config.webroot);
    if (!client->full_path) {
        log_message(LOG_ERROR, "Failed to resolve path");
        send_error_response(500, client);
        free_client(client);
        goto cleanup;
    }
    
    log_message(LOG_INFO, "Resolved path: %s", client->full_path);
    
    // Check cache
    struct Node* cache_node = cache_lookup(args->tree_head, client->full_path);
    
    // Check If-Modified-Since header
    if (cache_node && cache_node->last_modified && client->modified_since) {
        if (strcmp(cache_node->last_modified, client->modified_since) <= 0) {
            log_message(LOG_INFO, "Resource not modified (If-Modified-Since) - sending 304");
            send_not_modified_response(client, cache_node);
            free_client(client);
            goto cleanup;
        }
    }
    
    // Check ETag header
    if (cache_node && client->tag != 0) {
        if (cache_node->file_hash == client->tag) {
            log_message(LOG_INFO, "ETag match (client: %u, cache: %u) - sending 304", 
                       client->tag, cache_node->file_hash);
            send_not_modified_response(client, cache_node);
            free_client(client);
            goto cleanup;
        }
    }
    
    // Open requested file
    client->fd = open(client->full_path, O_RDONLY);
    if (client->fd < 0) {
        if (errno == ENOENT) {
            log_message(LOG_WARN, "File not found: %s", client->full_path);
            send_error_response(404, client);
        } else if (errno == EACCES) {
            log_message(LOG_WARN, "Permission denied: %s", client->full_path);
            send_error_response(403, client);
        } else {
            log_message(LOG_ERROR, "Failed to open file %s: %s", 
                       client->full_path, strerror(errno));
            send_error_response(500, client);
        }
        free_client(client);
        goto cleanup;
    }
    
    // Send file response (handles both GET and HEAD, handles ranges)
    int result = send_file_response(client, cache_node);
    if (result < 0) {
        log_message(LOG_ERROR, "Failed to send file response");
    }
    
    // Cleanup
    free_client(client);

cleanup:
    // Cleanup SSL
    if (args->ssl) {
        SSL_shutdown(args->ssl);
        SSL_free(args->ssl);
    }
    
    // Close socket
    close(args->client_fd);
    
    // Free thread arguments
    free(args);
    
    return NULL;
}

/**
 * Sets up and opens a new socket on a specified port
 *
 * Creates a socket, and specifies the option to rebind to the port if still open.
 *  This socket binds and listens on a specified port for IPv4 connections only.
 *
 * @param port Port for which OS will bind and listen for connections on
 *
 * @return File Descriptor for the new socket opened
 *
 * @note This function is called by both the HTTP and HTTPS sockets
 * @warning Caller must close() the returned socket when done
 */
int create_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Set SO_REUSEADDR to allow quick restart
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(sock);
        return -1;
    }
    
    // Bind to address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sock);
        return -1;
    }
    
    // Listen for connections
    if (listen(sock, BACKLOG) < 0) {
        perror("listen failed");
        close(sock);
        return -1;
    }
    
    printf("Server listening on port %d\n", port);
    return sock;
}

/**
 * Main control flow for the program
 * 
 * Sets up log files, ssl, HTTP and HTTPS sockets and threads. 
 *  Waits for new connections on both a HTTP and HTTPS socket. Invokes
 *  Handle Client Thread to handle client and closes. After main loop, 
 *  cleanup logs, ssl, and HTTP/HTTPS sockets.
 * 
 * @param argc Counts how many argument were passed in when executed
 * @param argv Stores the arguments passed in on execution
 *
 * @return 0 on successful shutdown, 1 on initialization error
 *
 * @note All clients are handled by handle_client_thread
 * @warning Must cleanup ThreadArgs object in handle_worker_thread
 * @see handle_client_thread(), setup_signals(), threadpool_create()
 */
int main(int argc, char** argv) {
    printf("=== HTTP/HTTPS Server Starting ===\n");
    printf("Server: Snap/0.4\n");
    printf("PID: %d\n", getpid());
    
    // Load configuration
    if (load_config(argc, argv) < 0) {
        fprintf(stderr, "Usage: %s -v <video_directory>\n", argv[0]);
        return 1;
    }
    
    extern ServerConfig g_config;
    
    // Initialize logger
    log_init("server.log");
    log_message(LOG_INFO, "Server starting - PID: %d", getpid());
    
    // Setup signal handlers
    setup_signals();
    
    // Initialize cache tree
    struct Node* cache_tree = cache_tree_init(g_config.webroot);
    if (!cache_tree) {
        log_message(LOG_ERROR, "Failed to initialize cache tree");
        return 1;
    }
    
    // Initialize OpenSSL
    init_openssl();
    SSL_CTX* ssl_ctx = create_ssl_context();
    if (!ssl_ctx) {
        log_message(LOG_ERROR, "Failed to create SSL context");
        cache_tree_free(cache_tree);
        cleanup_openssl();
        return 1;
    }
    configure_ssl_context(ssl_ctx);
    
    // Create HTTP socket
    int http_sock = create_server_socket(g_config.http_port);
    if (http_sock < 0) {
        log_message(LOG_ERROR, "Failed to create HTTP socket");
        SSL_CTX_free(ssl_ctx);
        cleanup_openssl();
        cache_tree_free(cache_tree);
        return 1;
    }
    
    // Create HTTPS socket
    int https_sock = create_server_socket(g_config.https_port);
    if (https_sock < 0) {
        log_message(LOG_ERROR, "Failed to create HTTPS socket");
        close(http_sock);
        SSL_CTX_free(ssl_ctx);
        cleanup_openssl();
        cache_tree_free(cache_tree);
        return 1;
    }
    
    // Create thread pool
    struct ThreadPoolConfig pool_config = {
        .num_threads = g_config.thread_pool_size,
        .max_queue_size = g_config.max_queue_size
    };
    
    g_thread_pool = threadpool_create(pool_config);
    if (!g_thread_pool) {
        log_message(LOG_ERROR, "Failed to create thread pool");
        close(http_sock);
        close(https_sock);
        SSL_CTX_free(ssl_ctx);
        cleanup_openssl();
        cache_tree_free(cache_tree);
        return 1;
    }
    
    log_message(LOG_INFO, "Server initialized successfully");
    printf("=== Server Ready ===\n");
    printf("HTTP Port: %d\n", g_config.http_port);
    printf("HTTPS Port: %d\n", g_config.https_port);
    printf("Thread Pool Size: %d\n", g_config.thread_pool_size);
    printf("Press Ctrl+C to shutdown\n");
    printf("Send SIGUSR1 (kill -USR1 %d) to refresh cache\n", getpid());
    
    // Main server loop
    while (!g_shutdown) {
        // Handle cache refresh signal
        if (g_refresh_cache) {
            log_message(LOG_INFO, "Refreshing cache tree");
            
            // Wait for pending work to complete
            threadpool_wait(g_thread_pool);
            
            // Refresh cache
            cache_tree_refresh(&cache_tree, g_config.webroot);
            
            g_refresh_cache = 0;
            log_message(LOG_INFO, "Cache refresh complete");
        }
        
        // Setup select() for both sockets
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(http_sock, &read_fds);
        FD_SET(https_sock, &read_fds);
        
        int max_fd = (http_sock > https_sock) ? http_sock : https_sock;
        
        // Use timeout so we can check signals periodically
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, continue to check shutdown flag
                continue;
            }
            log_message(LOG_ERROR, "select() failed: %s", strerror(errno));
            break;
        }
        
        if (activity == 0) {
            // Timeout, no activity - just loop again to check signals
            continue;
        }
        
        // Handle new HTTP connection
        if (FD_ISSET(http_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(http_sock, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                if (errno != EINTR) {
                    log_message(LOG_ERROR, "accept() failed on HTTP socket: %s", 
                               strerror(errno));
                }
                continue;
            }
            
            log_message(LOG_INFO, "New HTTP connection from %s:%d", 
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            // Allocate thread arguments
            ThreadArgs* args = malloc(sizeof(ThreadArgs));
            if (!args) {
                log_message(LOG_ERROR, "Failed to allocate thread args");
                close(client_fd);
                continue;
            }
            
            args->client_fd = client_fd;
            args->ssl = NULL;
            args->client_addr = client_addr;
            args->tree_head = cache_tree;
            
            // Submit to thread pool
            if (threadpool_add_work(g_thread_pool, handle_client_thread, args) != 0) {
                log_message(LOG_WARN, "Thread pool queue full, rejecting connection");
                close(client_fd);
                free(args);
            }
        }
        
        // Handle new HTTPS connection
        if (FD_ISSET(https_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(https_sock, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                if (errno != EINTR) {
                    log_message(LOG_ERROR, "accept() failed on HTTPS socket: %s", 
                               strerror(errno));
                }
                continue;
            }
            
            log_message(LOG_INFO, "New HTTPS connection from %s:%d", 
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            // Create SSL connection
            SSL* ssl = SSL_new(ssl_ctx);
            if (!ssl) {
                log_message(LOG_ERROR, "Failed to create SSL object");
                close(client_fd);
                continue;
            }
            
            SSL_set_fd(ssl, client_fd);
            
            // Perform SSL handshake
            if (SSL_accept(ssl) <= 0) {
                log_message(LOG_ERROR, "SSL handshake failed");
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                close(client_fd);
                continue;
            }
            
            log_message(LOG_DEBUG, "SSL handshake successful");
            
            // Allocate thread arguments
            ThreadArgs* args = malloc(sizeof(ThreadArgs));
            if (!args) {
                log_message(LOG_ERROR, "Failed to allocate thread args");
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_fd);
                continue;
            }
            
            args->client_fd = client_fd;
            args->ssl = ssl;
            args->client_addr = client_addr;
            args->tree_head = cache_tree;
            
            // Submit to thread pool
            if (threadpool_add_work(g_thread_pool, handle_client_thread, args) != 0) {
                log_message(LOG_WARN, "Thread pool queue full, rejecting connection");
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_fd);
                free(args);
            }
        }
    }
    
    // Shutdown sequence
    printf("\n=== Shutting down server ===\n");
    log_message(LOG_INFO, "Server shutdown initiated");
    
    // Stop accepting new connections
    close(http_sock);
    close(https_sock);
    printf("Closed listening sockets\n");
    
    // Wait for all pending work to complete
    printf("Waiting for pending requests to complete...\n");
    threadpool_wait(g_thread_pool);
    
    // Destroy thread pool
    printf("Destroying thread pool...\n");
    threadpool_destroy(g_thread_pool);
    
    // Cleanup cache tree
    printf("Freeing cache tree...\n");
    cache_tree_free(cache_tree);
    
    // Cleanup SSL
    printf("Cleaning up SSL...\n");
    SSL_CTX_free(ssl_ctx);
    cleanup_openssl();
    
    // Cleanup config
    free_config();
    
    // Close logger
    log_message(LOG_INFO, "Server shutdown complete");
    log_close();
    
    printf("=== Server stopped ===\n");
    return 0;
}