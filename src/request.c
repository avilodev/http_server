#include "request.h"
#include "response.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static void parse_header_line(Client* client, char* line);
static void parse_range_header(Client* client, char* range_value);

/**
 * Parses an HTTP/S request and creates a Client object
 *
 * Parses the request line (method, path, version) and all headers from a raw
 * HTTP/S request string. Validates the HTTP version and required headers,
 * returning a fully initialized Client structure.
 *
 * @param raw_request Raw HTTP request string received from the client
 * @param client_fd File descriptor for the client socket
 * @param ssl SSL structure for HTTPS connections, or NULL for HTTP
 *
 * @return Pointer to initialized Client structure, or NULL on error
 *
 * @note Returns NULL for: allocation failure, empty/malformed requests,
 *       unsupported HTTP versions, or missing required headers
 * @warning Caller must free the returned Client structure and all its members
 *
 * @see parse_header_line()
 */
Client* parse_http_request(char* raw_request, int client_fd, SSL* ssl) {
    // Allocate client structure
    Client* client = calloc(1, sizeof(Client));
    if (!client) {
        log_message(LOG_ERROR, "Failed to allocate client structure");
        return NULL;
    }
    
    client->client_fd = client_fd;
    client->fd = -1;  // No file open yet
    
    // SSL setup
    if (ssl) {
        client->is_ssl = 1;
        client->ssl = ssl;
    } else {
        client->is_ssl = 0;
        client->ssl = NULL;
    }
    
    // Initialize defaults
    client->range = 0;
    client->start_range = 0;
    client->end_range = -1;
    client->tag = 0;
    client->connection_status = 0;  // Default to close
    client->DNT = 0;
    client->GPC = 0;
    client->upgrade_tls = 0;
    
    // Parse request line
    char* saveptr;
    char* line = strtok_r(raw_request, "\r\n", &saveptr);
    
    if (!line) {
        log_message(LOG_WARN, "Empty HTTP request");
        send_error_response(400, client);
        free(client);
        return NULL;
    }
    
    // Parse: METHOD PATH VERSION
    char* token_saveptr;
    client->method = strtok_r(line, " ", &token_saveptr);
    client->path = strtok_r(NULL, " ", &token_saveptr);
    client->version = strtok_r(NULL, "\r\n", &token_saveptr);
    
    // Validate request line
    if (!client->method || !client->path || !client->version) {
        log_message(LOG_WARN, "Malformed request line");
        send_error_response(400, client);
        free(client);
        return NULL;
    }
    
    // Validate HTTP version
    if (strcmp(client->version, "HTTP/1.0") == 0) {
        client->connection_status = 0;
    } else if (strcmp(client->version, "HTTP/1.1") == 0) {
        client->connection_status = 1; 
    } else {
        log_message(LOG_WARN, "Unsupported HTTP version: %s", client->version);
        send_error_response(505, client);
        free(client);
        return NULL;
    }
    
    // Parse headers
    while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
        parse_header_line(client, line);
    }
    
    // Validate required headers (Host is required in HTTP/1.1)
    if (strcmp(client->version, "HTTP/1.1") == 0 && !client->host) {
        log_message(LOG_WARN, "Missing Host header in HTTP/1.1 request");
        send_error_response(400, client);
        free(client);
        return NULL;
    }
    
    return client;
}

/**
 * Parse a single HTTP header line and populate the Client structure.
 *
 * Recognizes common headers (Host, User-Agent, Range, etc.) and stores
 * values in client fields. Unknown headers are silently ignored.
 *
 * @param client Client structure to update
 * @param line Header line to parse (e.g., "Content-Type: text/html")
 *
 * @warning Client fields point into the line buffer—do not free source
 */
static void parse_header_line(Client* client, char* line) {
    if (strncasecmp(line, "Host: ", 6) == 0) {
        client->host = line + 6;
    }
    else if (strncasecmp(line, "Connection: ", 12) == 0) {
        if (strncasecmp(line + 12, "keep-alive", 10) == 0) {
            client->connection_status = 1;
        } else {
            client->connection_status = 0;
        }
    }
    else if (strncasecmp(line, "User-Agent: ", 12) == 0) {
        client->user_agent = line + 12;
    }
    else if (strncasecmp(line, "If-None-Match: ", 15) == 0) {
        char* etag = line + 15;
        // Remove quotes: "123" -> 123
        if (*etag == '"') etag++;
        char* end = strchr(etag, '"');
        if (end) *end = '\0';
        client->tag = (unsigned int)strtoul(etag, NULL, 10);
    }
    else if (strncasecmp(line, "If-Modified-Since: ", 19) == 0) {
        client->modified_since = line + 19;
    }
    else if (strncasecmp(line, "Range: ", 7) == 0) {
        parse_range_header(client, line + 7);
    }
    else if (strncasecmp(line, "DNT: ", 5) == 0) {
        client->DNT = (line[5] == '1') ? 1 : 0;
    }
    else if (strncasecmp(line, "Sec-GPC: ", 9) == 0) {
        client->GPC = (line[9] == '1') ? 1 : 0;
    }
    else if (strncasecmp(line, "Upgrade-Insecure-Requests: ", 27) == 0) {
        client->upgrade_tls = (line[27] == '1') ? 1 : 0;
    }
    else if (strncasecmp(line, "Referer: ", 9) == 0) {
        client->referer = line + 9;
    }
    else if (strncasecmp(line, "Accept: ", 8) == 0) {
        client->accept = line + 8;
    }
    else if (strncasecmp(line, "Accept-Encoding: ", 17) == 0) {
        client->encoding = line + 17;
    }
    else if (strncasecmp(line, "Accept-Language: ", 17) == 0) {
        client->language = line + 17;
    }
    else if (strncasecmp(line, "Priority: ", 10) == 0) {
        client->priority = line + 10;
    }
}

/**
 * Parses the range attribute of a HTTP/S header
 *
 * Validates and updates the client struct for the Range header.
 * If Range request is poorly formatted, it is silently ignored.
 *
 * @param client Client structure to update
 * @param range_value Range request line to parse
 */
static void parse_range_header(Client* client, char* range_value) {
    if (strncmp(range_value, "bytes=", 6) != 0) {
        client->range = 0;
        return;
    }
    
    client->range = 1;
    range_value += 6;
    
    // Default values
    client->start_range = 0;
    client->end_range = -1;  // -1 means "to end of file"
    
    if (*range_value == '-') {
        // Suffix range: bytes=-500 (last 500 bytes)
        client->start_range = -atoll(range_value + 1);
    } else {
        // Regular range: bytes=0-1023 or bytes=1000-
        char* dash = strchr(range_value, '-');
        if (dash) {
            *dash = '\0';
            client->start_range = atoll(range_value);
            dash++;
            
            // Skip whitespace
            while (*dash == ' ' || *dash == '\t') dash++;
            
            // Parse end if present
            if (*dash >= '0' && *dash <= '9') {
                client->end_range = atoll(dash);
            }
            // else end_range stays -1 (to EOF)
        }
    }
}

/**
 * Validate the HTTP/S method requested
 *
 * Checks that the HTTP/S method is either GET/HEAD/OPTIONS. 
 * Anything else will return 0.
 *
 * @param method From the client struct, the HTTP/S method from client request 
 *
 * @return Returns 1 for a valid HTTP/S method, 0 for an unrecognized or unsupported method.
 *
 * @warning Does not check whether the method is a valid HTTP/S method
 */
int validate_http_method(const char* method) {
    if (!method) return 0;
    
    // Supported methods
    if (strcmp(method, "GET") == 0) return 1;
    if (strcmp(method, "HEAD") == 0) return 1;
    if (strcmp(method, "OPTIONS") == 0) return 1;
    
    return 0;
}

/**
 * Validate the HTTP/S path requested
 *
 * Checks that the HTTP/S path does not ask for any resources 
 * outside of the designated path. If ".." or "//" is requested,
 * return 0. If not, reutrn 1.
 *
 * @param path From the client struct, the HTTP/S path from client request.
 *
 * @return Returns 1 for a valid HTTP/S path, 0 for a restricted path.
 *
 * @warning Does not validate if the resource exists. Only if the path is valid.
 */
int validate_path(const char* path) {
    if (!path) return 0;
    
    // Check for path traversal
    if (strstr(path, "..") != NULL) return 0;
    if (strstr(path, "//") != NULL) return 0;
    
    // Check for null bytes
    if (strlen(path) != strcspn(path, "\0")) return 0;
    
    return 1;
}

/**
 * Builds the request path from the path variable in the client struct.
 *
 * Builds the path of the resource requested by the client. Uses the webroot,
 * webpages/ folder and resource requested to return the full path. If the 
 * client wants the homepage, itll also return that path.
 *
 * @param request_path Client requested path.
 * @param webroot Path where web files are stored.
 *
 * @return Returns the resolved path. Will never return NULL.
 *
 * @warning Returned string must be freed elsewhere.
 */
char* resolve_request_path(const char* request_path, const char* webroot) {
    char resolved[4096];
    
    // Handle root request
    const char* page = (strcmp(request_path, "/") == 0) ? "/landing.html" : request_path;
    
    // Build full path
    snprintf(resolved, sizeof(resolved), "%s/webpages%s", webroot, page);
    
    return strdup(resolved);
}

/**
 * Frees all client struct data.
 *
 * Frees client struct and closes the client socket. Also frees
 * path data returned from resolve_request_path().
 *
 * @param request_path Client requested path.
 * @param webroot Path where web files are stored.
 *
 * @note Other client struct data is freed elsewhere.
 */
void free_client(Client* client) {
    if (!client) return;
    
    if (client->fd >= 0) {
        close(client->fd);
    }
    
    if (client->full_path) {
        free(client->full_path);
    }
    
    // Note: Don't free string pointers like client->method, client->path, etc.
    // They point into the raw_request buffer which is managed elsewhere
    
    free(client);
}

/**
 * Prints client request 
 *
 * Prints client request like Method, Path, Version, Host, etc.
 * Used for debug purposes only.
 *
 * @param client Client request data used for printing.
 *
 * @warning Does not validate all of these fields. Some may 
 * not have been initialzed by the client.
 */
void print_client_info(const Client* client) {
    if (!client) return;
    
    log_message(LOG_DEBUG, "=== Client Request ===");
    log_message(LOG_DEBUG, "%s %s %s", client->method, client->path, client->version);
    log_message(LOG_DEBUG, "Host: %s", client->host ? client->host : "(none)");
    log_message(LOG_DEBUG, "Connection: %s", client->connection_status ? "keep-alive" : "close");
    log_message(LOG_DEBUG, "ETag: %u", client->tag);
    log_message(LOG_DEBUG, "Range: %d (start=%ld, end=%ld)", 
                client->range, client->start_range, client->end_range);
    log_message(LOG_DEBUG, "SSL: %d", client->is_ssl);
    log_message(LOG_DEBUG, "=====================");
}