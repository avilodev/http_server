#include "api.h"
#include "session.h"

ApiRoute api_routes[] = {
    { "/api/status", handle_api_status },
    { "/api/info", handle_api_info },
    { "/api/files",  handle_api_files },
    { "/api/config", handle_api_config },
    { "/api/time", handle_api_time },
    { "/api/logout", handle_api_logout },
    { NULL, NULL }
};

void handle_api_request(Client* client) 
{
    for (int i = 0; api_routes[i].path != NULL; i++) {
        size_t route_len = strlen(api_routes[i].path);
        /* Match the route prefix and require the next character to be '?' or '\0'
         * so that /api/files?path=... correctly dispatches to handle_api_files. */
        if (strncmp(client->path, api_routes[i].path, route_len) == 0 &&
            (client->path[route_len] == '\0' || client->path[route_len] == '?')) {
            api_routes[i].handler(client);
            return;
        }
    }
    send_api_error(client, 404, "NOT_FOUND", "Request Not Found");
}


void handle_api_status(Client* client)
{
    extern time_t g_server_start;
    long uptime = (long)(time(NULL) - g_server_start);

    char response[128];
    snprintf(response, sizeof(response),
        "{\"status\":\"online\",\"uptime\":%ld,\"version\":\"0.4\"}", uptime);

    send_api_response(client, 200, "application/json", response);
}

void handle_api_info(Client* client) 
{
    extern ServerConfig g_config;
    
    char response[512];
    snprintf(response, sizeof(response),
        "{\n"
        "  \"success\": true,\n"
        "  \"data\": {\n"
        "    \"name\": \"Snap\",\n"
        "    \"version\": \"0.4\",\n"
        "    \"http_port\": %d,\n"
        "    \"https_port\": %d,\n"
        "    \"ssl_enabled\": true,\n"
        "    \"features\": [\"http\", \"https\", \"range-requests\", \"caching\"]\n"
        "  }\n"
        "}",
        g_config.http_port,
        g_config.https_port
    );
    
    send_api_response(client, 200, "application/json", response);
}

void handle_api_files(Client* client) {
    char* path = get_query_param(client, "path");
    /* get_query_param returns a heap-allocated string; free after use. */
    const char* effective_path = path ? path : "/";

    char full_path[512];
    extern ServerConfig g_config;
    snprintf(full_path, sizeof(full_path), "%s/public/%s", g_config.webroot, effective_path);

    DIR* dir = opendir(full_path);
    if (!dir) {
        free(path);
        send_api_error(client, 404, "NOT_FOUND", "Directory not found");
        return;
    }

    // Build JSON array of files
    char response[4096];
    int offset = snprintf(response, sizeof(response),
        "{\n  \"success\": true,\n  \"data\": {\n    \"path\": \"%s\",\n    \"files\": [\n",
        effective_path
    );
    
    struct dirent* entry;
    int first = 1;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        struct stat st;
        char file_path[1024];  // <-- INCREASE THIS (512 + 255 + some buffer)
        snprintf(file_path, sizeof(file_path), "%s/%s", full_path, entry->d_name);
        
        if (stat(file_path, &st) == 0) {
            if (!first) {
                offset += snprintf(response + offset, sizeof(response) - offset, ",\n");
            }
            first = 0;
            
            // Check if we have enough space left in response buffer
            if (offset >= (int)sizeof(response) - 200) {
                break;  // Prevent buffer overflow
            }
            
            offset += snprintf(response + offset, sizeof(response) - offset,
                "      {\n"
                "        \"name\": \"%s\",\n"
                "        \"type\": \"%s\",\n"
                "        \"size\": %ld,\n"
                "        \"modified\": %ld\n"
                "      }",
                entry->d_name,
                S_ISDIR(st.st_mode) ? "directory" : "file",
                st.st_size,
                (long)st.st_mtime
            );
        }
    }
    
    offset += snprintf(response + offset, sizeof(response) - offset,
        "\n    ]\n  }\n}"
    );
    
    closedir(dir);
    free(path);
    send_api_response(client, 200, "application/json", response);
}



void handle_api_config(Client* client)
{
    extern ServerConfig g_config;

    char response[512];
    snprintf(response, sizeof(response),
        "{\n"
        "  \"success\": true,\n"
        "  \"data\": {\n"
        "    \"http_port\": %d,\n"
        "    \"https_port\": %d,\n"
        "    \"thread_pool_size\": %d,\n"
        "    \"max_queue_size\": %d,\n"
        "    \"webroot\": \"%s\"\n"
        "  }\n"
        "}",
        g_config.http_port,
        g_config.https_port,
        g_config.thread_pool_size,
        g_config.max_queue_size,
        g_config.webroot ? g_config.webroot : ""
    );

    send_api_response(client, 200, "application/json", response);
}

void handle_api_time(Client* client) 
{
    char* date = get_time(0);
    
    // Build proper JSON response
    char response[512];
    snprintf(response, sizeof(response),
        "{\n"
        "  \"success\": true,\n"
        "  \"data\": {\n"
        "    \"timestamp\": \"%s\",\n"
        "    \"timezone\": \"UTC\",\n"
        "    \"unix\": %ld\n"
        "  }\n"
        "}",
        date,
        time(NULL)
    );
    
    send_api_response(client, 200, "application/json", response);
    
    free(date);
}

void handle_api_logout(Client* client)
{
    if (client->session_token) {
        session_destroy(client->session_token);
    }
    send_login_redirect("/login.html", "", 0, client);
}

void send_api_error(Client* client, int status_code, const char* error_code, const char* message) {
    char response[512];
    snprintf(response, sizeof(response),
        "{\n"
        "  \"success\": false,\n"
        "  \"error\": {\n"
        "    \"code\": \"%s\",\n"
        "    \"message\": \"%s\",\n"
        "    \"status\": %d\n"
        "  }\n"
        "}",
        error_code,
        message,
        status_code
    );
    
    send_api_response(client, status_code, "application/json", response);
}