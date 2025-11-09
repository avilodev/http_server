#include "hash_table.h"

ht* init_hash(void)
{
    // Allocate space for hash table struct.
    ht* table = malloc(sizeof(ht)); 
    if (table == NULL) {
        return NULL;
    }
    table->length = 0;
    table->capacity = LARGE_ALLOCATE;

    // Allocate (zero'd) space for entry buckets.
    table->entries = calloc(table->capacity, sizeof(ht_entry));
    if (table->entries == NULL) {
        free(table);
        return NULL;
    }
    return table;
}

static uint64_t hash_key(const char* key) {
    uint64_t hash = FNV_OFFSET;
    for (const char* p = key; *p; p++) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

// Keep your existing static ht_set as is, but rename it
static const char* ht_set_entry(ht_entry* entries, size_t capacity, 
                                 const char* key, void* value, size_t* plength) {
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(capacity - 1));

    while (entries[index].key != NULL) {
        if (strcmp(key, entries[index].key) == 0) {
            entries[index].value = value;
            return entries[index].key;
        }
        index++;
        if (index >= capacity) {
            index = 0;
        }
    }

    if (plength != NULL) {
        key = strdup(key);
        if (key == NULL) {
            return NULL;
        }
        (*plength)++;
    }
    entries[index].key = (char*)key;
    entries[index].value = value;
    return key;
}

// Add new public function
const char* ht_set(ht* table, const char* key, void* value) {
    if (table == NULL) {
        return NULL;
    }
    return ht_set_entry(table->entries, table->capacity, key, value, &table->length);
}

void* ht_get(ht* table, const char* key) {
    // AND hash with capacity-1 to ensure it's within entries array.
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(table->capacity - 1));

    // Loop till we find an empty entry.
    while (table->entries[index].key != NULL) {
        if (strcmp(key, table->entries[index].key) == 0) {
            // Found key, return value.
            return table->entries[index].value;
        }
        // Key wasn't in this slot, move to next (linear probing).
        index++;
        if (index >= table->capacity) {
            // At end of entries array, wrap around.
            index = 0;
        }
    }
    return NULL;
}

void ht_destroy(ht* table) {
    if (table == NULL) return;
    
    // Track unique MIME values to avoid double-free
    char** mime_values = malloc(sizeof(char*) * table->length);
    size_t mime_count = 0;
    
    // First pass: collect unique MIME values
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].key != NULL) {
            char* mime = (char*)table->entries[i].value;
            
            // Check if we've already seen this MIME value
            int found = 0;
            for (size_t j = 0; j < mime_count; j++) {
                if (mime_values[j] == mime) {
                    found = 1;
                    break;
                }
            }
            
            if (!found && mime != NULL) {
                mime_values[mime_count++] = mime;
            }
            
            // Free the key (extension)
            free((void*)table->entries[i].key);
        }
    }
    
    // Free unique MIME values
    for (size_t i = 0; i < mime_count; i++) {
        free(mime_values[i]);
    }
    
    free(mime_values);
    free(table->entries);
    free(table);
}