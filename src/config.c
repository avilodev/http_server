#include "config.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global configuration
struct ServerConfig g_config;

/**
 * Sets defaults for the server to boot up.
 *
 * Default ports, webroots, key paths, threads, and queue sizes are set here.
 *
 * @warning Certificate,Key paths and webroot for the server must be freed later.
 */
static void init_default_config(void) {
    g_config.webroot = strdup(SERVER_PATH);
    g_config.http_port = 80;
    g_config.https_port = 443;

    char cert_path[SMALL_ALLOCATE];
    char server_key_path[SMALL_ALLOCATE];

    sprintf(cert_path, "%s/keys/cert.pem", SERVER_PATH);
    sprintf(server_key_path, "%s/keys/key.pem", SERVER_PATH);

    g_config.cert_path = strdup(cert_path);
    g_config.key_path = strdup(server_key_path);
    g_config.thread_pool_size = 20;
    g_config.max_queue_size = 100;
}

/**
 * Updates the arguments for the server startup configuration.
 * 
 * Calls init_default_config() to set default server configuration, then
 * update webroot, ports, and thread sizes through server flags. If a 
 * parameter is unknown, it returns an error. Otherwise successful.
 * 
 * @param argc Counts how many argument were passed in when executed
 * @param argv Stores the arguments passed in on execution
 *
 * @return 0 on successful updates, -1 on unknown parameters.
 *
 * @see init_default_config()
 */
int load_config(int argc, char** argv) {
    // Initialize defaults
    init_default_config();
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "w:p:s:t:")) != -1) {
        switch (opt) {
            case 'w':
                free(g_config.webroot);
                g_config.webroot = strdup(optarg);
                break;
            case 'p':
                g_config.http_port = atoi(optarg);
                break;
            case 's':
                g_config.https_port = atoi(optarg);
                break;
            case 't':
                g_config.thread_pool_size = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-w webroot] [-p http_port] [-s https_port] [-t threads]\n", argv[0]);
                return -1;
        }
    }
    
    printf("Configuration loaded:\n");
    printf("  Webroot: %s\n", g_config.webroot);
    printf("  HTTP port: %d\n", g_config.http_port);
    printf("  HTTPS port: %d\n", g_config.https_port);
    printf("  Thread pool: %d\n", g_config.thread_pool_size);
    
    return 0;
}

/**
 * Frees server config information allocated in init_default_config().
 *
 * @see init_default_config()
 */
void free_config(void) {
    if (g_config.webroot) {
        free(g_config.webroot);
        g_config.webroot = NULL;
    }
    if (g_config.cert_path) {
        free(g_config.cert_path);
        g_config.cert_path = NULL;
    }
    if (g_config.key_path) {
        free(g_config.key_path);
        g_config.key_path = NULL;
    }
}