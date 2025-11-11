#include "utils.h"

/**
 * Gets the current time added to by an offset. 
 *
 * @param offset Date offset in seconds. 
 *
 * @return Date specificed by current time and offset
 *
 * @warning Caller must free returned date
 */
char* get_time(int offset)
{
    time_t now = time(NULL);
    now += offset;
    struct tm tm = *gmtime(&now);

    char* str = malloc(LARGE_ALLOCATE);
    strftime(str, LARGE_ALLOCATE, "%a, %d %b %Y %H:%M:%S GMT", &tm);

    return str;
}

// Parse query parameter from URL
// Example: /api/files?path=/videos returns "/videos" for key "path"
const char* get_query_param(Client* client, const char* key) 
{
    if (!client || !client->path || !key) {
        return NULL;
    }
    
    // Find '?' in path
    char* query_start = strchr(client->path, '?');
    if (!query_start) {
        return NULL;
    }
    
    query_start++;  // Skip the '?'
    
    // Search for key=value
    static char value[256];  // Static buffer (not thread-safe, but simple)
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    
    char* param_start = strstr(query_start, search_key);
    if (!param_start) {
        return NULL;
    }
    
    param_start += strlen(search_key);
    
    // Copy until '&' or end of string
    int i = 0;
    while (param_start[i] && param_start[i] != '&' && i < 255) {
        value[i] = param_start[i];
        i++;
    }
    value[i] = '\0';
    
    return value;
}