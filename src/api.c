#include "api.h"

ApiRoute api_routes[] = {
    { "/api/status", handle_api_status },
    { "/api/files",  handle_api_files },
    { "/api/config", handle_api_config },
    { "/api/time", handle_api_time },
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

void handle_api_time(Client* client)
{
    char current_time[SMALL_ALLOCATE];
    char* date = get_time(0);

    sprintf(current_time, "\"status\":\"online\",\"date\":\"%s\"", date);

    send_api_response(client, 200, "application/json", current_time);

    free(date);
}