#include "api.h"

ApiRoute api_routes[] = {
    { "/api/status", handle_api_status },
    { "/api/files",  handle_api_files },
    { "/api/config", handle_api_config },
    { NULL, NULL }
};

void handle_api_request(Client* client) {
    for (int i = 0; api_routes[i].path != NULL; i++) {
        if (strcmp(client->path, api_routes[i].path) == 0) {
            api_routes[i].handler(client);
            return;
        }
    }
    send_api_response(client, 404, "application/json", "{\"error\":\"Unknown API endpoint\"}");
}


void handle_api_status(Client* client)
{
    send_api_response(client, 200, "application/json", "\"status\":\"online\",\"uptime\":102,\"version\":\"0.4\"");
}
 

void handle_api_files(Client* client)
{
    send_api_response(client, 200, "application/json", "Files");
}


void handle_api_config(Client* client)
{
    send_api_response(client, 200, "application/json", "Config");
}