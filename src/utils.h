#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>

#include "types.h"

char* get_time(int offset);
const char* get_query_param(Client* client, const char* key);

#endif 