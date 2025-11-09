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