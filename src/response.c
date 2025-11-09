#include "response.h"
#include "logger.h"
#include "node.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define MAX_HEADER_SIZE 8192
#define BUFFER_SIZE 65536

/**
 * Sends a complete file response with proper HTTP headers
 *
 * Handles both full file responses (200 OK) and partial content responses
 * (206 Partial Content) for byte-range requests. Supports HEAD requests by
 * sending only headers without body content. Automatically handles SSL/TLS
 * connections and generates appropriate cache validation headers.
 *
 * @param client Pointer to Client structure containing request details and
 *               connection information
 * @param cache_node Pointer to cache Node containing file metadata (ETag,
 *                   Last-Modified), or NULL if not cached
 *
 * @return 0 on success, -1 on error
 *
 * @note Handles range request validation and sends 416 if range is invalid
 * @note Gracefully handles client disconnections during transfer (ECONNRESET)
 * @note For HEAD requests, only headers are sent (no file content)
 * @warning Requires client->fd to be a valid open file descriptor
 *
 * @see send_error_response(), mime_get_type_from_filename()
 */
int send_file_response(Client* client, struct Node* cache_node) {
    if (!client || client->fd < 0) {
        log_message(LOG_ERROR, "Invalid client or file descriptor");
        return -1;
    }

    // Get file size
    struct stat st;
    if (fstat(client->fd, &st) < 0) {
        log_message(LOG_ERROR, "fstat failed: %s", strerror(errno));
        send_error_response(500, client);
        return -1;
    }
    
    off_t file_size = st.st_size;
    off_t start = 0;
    off_t end = file_size - 1;
    int is_partial = 0;
    
    // Handle range requests
    if (client->range) {
        is_partial = 1;
        
        // Handle suffix range (last N bytes)
        if (client->start_range < 0) {
            off_t suffix_len = -client->start_range;
            start = (file_size > suffix_len) ? (file_size - suffix_len) : 0;
            end = file_size - 1;
        } else {
            start = client->start_range;
            
            if (client->end_range > 0 && client->end_range < file_size) {
                end = client->end_range;
            } else {
                end = file_size - 1;
            }
        }
        
        // Validate range
        if (start >= file_size || start < 0 || end < start) {
            log_message(LOG_WARN, "Invalid range: %ld-%ld for file size %ld", 
                       start, end, file_size);
            send_error_response(416, client);
            return -1;
        }
    }
    
    off_t content_length = end - start + 1;
    
    // Build response headers
    char headers[MAX_HEADER_SIZE];
    int header_len = 0;
    
    // Status line
    if (is_partial) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "%s 206 Partial Content\r\n", client->version);
    } else {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "%s 200 OK\r\n", client->version);
    }
    
    extern ht* mime_table;

    // Headers
    const char* content_type = mime_get_type_from_filename(mime_table, client->full_path);
    char* current_date = get_current_http_date();
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Type: %s\r\n", content_type);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Length: %ld\r\n", content_length);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Accept-Ranges: bytes\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Date: %s\r\n", current_date);
    
    // Cache headers
    if (cache_node && !client->is_ssl) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "ETag: \"%u\"\r\n", cache_node->file_hash);
    }
    
    if (cache_node && cache_node->last_modified) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "Last-Modified: %s\r\n", cache_node->last_modified);
    }
    
    // Range header
    if (is_partial) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "Content-Range: bytes %ld-%ld/%ld\r\n", 
                              start, end, file_size);
    }
    
    // Connection header
    if (client->connection_status) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "Connection: keep-alive\r\n");
    } else {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "Connection: close\r\n");
    }
    
    // End headers
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len, "\r\n");
    
    free(current_date);
    
    // Send headers
    ssize_t sent;
    if (client->is_ssl) {
        sent = SSL_write(client->ssl, headers, header_len);
    } else {
        sent = send(client->client_fd, headers, header_len, 0);
    }
    
    if (sent < 0) {
        log_message(LOG_ERROR, "Failed to send headers");
        return -1;
    }
    
    // For HEAD requests, stop here
    if (strcmp(client->method, "HEAD") == 0) {
        log_message(LOG_INFO, "HEAD request - headers only");
        return 0;
    }
    
    // Send file content
    if (lseek(client->fd, start, SEEK_SET) < 0) {
        log_message(LOG_ERROR, "lseek failed: %s", strerror(errno));
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    off_t remaining = content_length;
    off_t total_sent = 0;
    
    while (remaining > 0) {
        int size_to_read = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
        
        ssize_t bytes_read = read(client->fd, buffer, size_to_read);
        if (bytes_read <= 0) {
            if (bytes_read < 0 && errno == EINTR) continue;
            break;
        }
        
        // Send data
        ssize_t bytes_sent;
        if (client->is_ssl) {
            bytes_sent = SSL_write(client->ssl, buffer, bytes_read);
        } else {
            bytes_sent = send(client->client_fd, buffer, bytes_read, 0);
        }
        
        if (bytes_sent <= 0) {
            // Client disconnected (normal for video seeking)
            if (errno == ECONNRESET || errno == EPIPE) {
                log_message(LOG_INFO, "Client disconnected (sent %ld/%ld bytes)",
                           total_sent, content_length);
                return 0;
            }
            log_message(LOG_ERROR, "Send failed: %s", strerror(errno));
            return -1;
        }
        
        remaining -= bytes_read;
        total_sent += bytes_read;
    }
    
    log_message(LOG_INFO, "Sent %ld bytes (status %d)", 
               total_sent, is_partial ? 206 : 200);
    
    return 0;
}

/**
 * Sends an HTTP error response with formatted HTML error page
 *
 * Generates a complete HTTP error response including status line, headers,
 * and an HTML error page body. Automatically determines the appropriate
 * status message and formats the response according to HTTP/1.0 or HTTP/1.1
 * standards. Always closes the connection after sending.
 *
 * @param status_code HTTP status code (e.g., 400, 403, 404, 500)
 * @param client Pointer to Client structure containing connection details
 *
 * @return 0 on success, -1 if client is NULL
 *
 * @note Uses generic "Snap/0.4" server signature in error page
 * @note Always sets Connection: close header
 * @note Handles both SSL and non-SSL connections automatically
 *
 * @see get_status_message(), send_file_response()
 */
int send_error_response(int status_code, Client* client) {
    if (!client) return -1;
    
    const char* status_msg = get_status_message(status_code);
    char* current_date = get_current_http_date();
    
    // Build error page
    char body[2048];
    int body_len = snprintf(body, sizeof(body),
        "<html>\n"
        "<head><title>%d %s</title></head>\n"
        "<body>\n"
        "<h1>%d %s</h1>\n"
        "<hr>\n"
        "<p>Snap/0.4</p>\n"
        "</body>\n"
        "</html>\n",
        status_code, status_msg, status_code, status_msg);
    
    // Build headers
    char headers[MAX_HEADER_SIZE];
    int header_len = 0;
    
    const char* version = client->version ? client->version : "HTTP/1.1";
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "%s %d %s\r\n", version, status_code, status_msg);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Type: text/html\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Length: %d\r\n", body_len);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Date: %s\r\n", current_date);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Connection: close\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len, "\r\n");
    
    free(current_date);
    
    // Send response
    if (client->is_ssl) {
        SSL_write(client->ssl, headers, header_len);
        SSL_write(client->ssl, body, body_len);
    } else {
        send(client->client_fd, headers, header_len, 0);
        send(client->client_fd, body, body_len, 0);
    }
    
    log_message(LOG_INFO, "Sent error %d to client", status_code);
    
    return 0;
}

/**
 * Sends a 304 Not Modified response for cache validation
 *
 * Used when client's cached version is still valid based on ETag or
 * Last-Modified comparison. Includes minimal headers (no body content)
 * to indicate the client should use its cached copy.
 *
 * @param client Pointer to Client structure containing request details
 * @param cache_node Pointer to cache Node containing ETag and Last-Modified
 *                   metadata, or NULL
 *
 * @return 0 on success, -1 on error
 *
 * @note Only includes ETag header for non-SSL connections
 * @note No body content is sent with 304 responses
 *
 * @see send_file_response()
 */
int send_not_modified_response(Client* client, struct Node* cache_node) {
    char headers[MAX_HEADER_SIZE];
    int header_len = 0;
    
    char* current_date = get_current_http_date();
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "%s 304 Not Modified\r\n", client->version);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Date: %s\r\n", current_date);
    
    if (cache_node && !client->is_ssl) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "ETag: \"%u\"\r\n", cache_node->file_hash);
    }
    
    if (cache_node && cache_node->last_modified) {
        header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                              "Last-Modified: %s\r\n", cache_node->last_modified);
    }
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len, "\r\n");
    
    free(current_date);
    
    if (client->is_ssl) {
        SSL_write(client->ssl, headers, header_len);
    } else {
        send(client->client_fd, headers, header_len, 0);
    }
    
    log_message(LOG_INFO, "Sent 304 Not Modified");
    
    return 0;
}

/**
 * Returns human-readable status message for HTTP status code
 *
 * Maps standard HTTP status codes to their corresponding reason phrases
 * as defined in RFC 7231. Returns "Unknown" for unrecognized codes.
 *
 * @param code HTTP status code (200, 404, 500, etc.)
 *
 * @return Pointer to static string containing status message
 *
 * @note Return value points to static memory - do not free
 * @note Includes RFC 2324 status 418 "I'm a teapot"
 */
const char* get_status_message(int code) {
    switch (code) {
        case 200: return "OK";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 416: return "Range Not Satisfiable";
        case 418: return "I'm a teapot";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown";
    }
}

/**
 * Formats a Unix timestamp as HTTP-date (RFC 7231)
 *
 * Converts a Unix timestamp to HTTP-date format required by HTTP headers
 * (e.g., "Mon, 01 Jan 2024 12:00:00 GMT"). Uses GMT timezone as required
 * by HTTP specification.
 *
 * @param timestamp Unix timestamp (seconds since epoch)
 *
 * @return Dynamically allocated string in HTTP-date format
 *
 * @warning Caller must free the returned string
 *
 * @see get_current_http_date()
 */
char* format_http_date(time_t timestamp) {
    static char buffer[64];
    struct tm* tm_info = gmtime(&timestamp);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return strdup(buffer);
}

/**
 * Gets current time formatted as HTTP-date string
 *
 * Convenience wrapper around format_http_date() that returns the current
 * system time in HTTP-date format for use in Date headers.
 *
 * @return Dynamically allocated string with current time in HTTP-date format
 *
 * @warning Caller must free the returned string
 *
 * @see format_http_date()
 */
char* get_current_http_date(void) {
    return format_http_date(time(NULL));
}

/**
 * Sends a 301 Moved Permanently redirect response
 *
 * Generates a redirect response directing the client to a new location.
 * Commonly used for directory redirects (e.g., /path to /path/) or
 * permanent URL changes. Always closes the connection after sending.
 *
 * @param location Target URL for redirection (absolute or relative)
 * @param client Pointer to Client structure containing connection details
 *
 * @return 0 on success, -1 if client or location is NULL
 *
 * @note Always sets Connection: close header
 * @note No body content is sent with redirect
 *
 * @see send_error_response()
 */
int send_redirect_response(const char* location, Client* client) {
    if (!client || !location) return -1;
    
    char headers[MAX_HEADER_SIZE];
    int header_len = 0;
    char* current_date = get_current_http_date();
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "%s 301 Moved Permanently\r\n", client->version);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Location: %s\r\n", location);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Date: %s\r\n", current_date);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Connection: close\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len, "\r\n");
    
    free(current_date);
    
    if (client->is_ssl) {
        SSL_write(client->ssl, headers, header_len);
    } else {
        send(client->client_fd, headers, header_len, 0);
    }
    
    log_message(LOG_INFO, "Sent 301 redirect to %s", location);
    return 0;
}

/**
 * Sends an OPTIONS response listing allowed HTTP methods
 *
 * Responds to HTTP OPTIONS requests by listing the HTTP methods supported
 * by the server (GET, HEAD, OPTIONS). Used for CORS preflight and server
 * capability discovery.
 *
 * @param client Pointer to Client structure containing connection details
 *
 * @return 0 on success, -1 if client is NULL
 *
 * @note Returns "Allow: GET, HEAD, OPTIONS" header
 * @note No body content is sent (Content-Length: 0)
 *
 * @see send_file_response()
 */
int send_options_response(Client* client) {
    if (!client) return -1;
    
    char headers[MAX_HEADER_SIZE];
    int header_len = 0;
    char* current_date = get_current_http_date();
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "%s 200 OK\r\n", client->version);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Allow: GET, HEAD, OPTIONS\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Date: %s\r\n", current_date);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Length: 0\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len, "\r\n");
    
    free(current_date);
    
    if (client->is_ssl) {
        SSL_write(client->ssl, headers, header_len);
    } else {
        send(client->client_fd, headers, header_len, 0);
    }
    
    log_message(LOG_INFO, "Sent OPTIONS response");
    return 0;
}

/**
 * Sends a 416 Range Not Satisfiable response
 *
 * Indicates that the requested byte range is invalid or outside the file's
 * bounds. Includes Content-Range header showing the valid file size to help
 * clients understand the acceptable range.
 *
 * @param client Pointer to Client structure containing connection details
 * @param file_size Total size of the requested file in bytes
 *
 * @return 0 on success, -1 if client is NULL
 *
 * @note Includes "Content-Range: bytes *file_size" header
 * @note No body content is sent
 *
 * @see send_file_response()
 */

int send_range_not_satisfiable(Client* client, off_t file_size) {
    if (!client) return -1;
    
    char headers[MAX_HEADER_SIZE];
    int header_len = 0;
    char* current_date = get_current_http_date();
    
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "%s 416 Range Not Satisfiable\r\n", client->version);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Range: bytes */%ld\r\n", file_size);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Date: %s\r\n", current_date);
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len,
                          "Content-Length: 0\r\n");
    header_len += snprintf(headers + header_len, MAX_HEADER_SIZE - header_len, "\r\n");
    
    free(current_date);
    
    if (client->is_ssl) {
        SSL_write(client->ssl, headers, header_len);
    } else {
        send(client->client_fd, headers, header_len, 0);
    }
    
    log_message(LOG_INFO, "Sent 416 Range Not Satisfiable");
    return 0;
}