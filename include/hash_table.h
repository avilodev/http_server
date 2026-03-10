// logger.h
#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#define FNV_OFFSET 146959810393
#define FNV_PRIME 1099511628211

#include "types.h"

typedef struct {
    const char* key;
    void* value;
} ht_entry;


typedef struct hash_table {
    ht_entry* entries;
    size_t capacity;
    size_t length;
} ht;

ht* init_hash(void);
void* ht_get(ht* table, const char* key);
const char* ht_set(ht* table, const char* key, void* value);
void ht_destroy(ht* table);

#endif