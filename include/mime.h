#ifndef MIME_H
#define MIME_H

#include "types.h"
#include "hash_table.h"

ht* mime_init(const char* filepath);
const char* mime_get_type(ht* table, const char* extension);
const char* mime_get_type_from_filename(ht* table, const char* filename);

#endif 
