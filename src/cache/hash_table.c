#include "hash_table.h"

/**
 * Initializes a hash table and required memory.
 *
 * Allocates memory for the new hash table, length,
 * for the slots of the hash table, and capacity. Also 
 * allocates memory using table->entires. 
 *
 * @return Newly initialized hash table.
 *
 * @note Returns NULL for failure to allocate memory.
 * @warning Caller must free the returned hash table
 */
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

/**
 * Takes a string and returns the resulting hash
 *
 * Uses a large prime number and every character of the 
 * parameter to create a corresponding u_int64_t hash. Uses
 * a mix of exponential and mulitplation logic.
 *
 * @param key Character string to be hashed
 *
 * @return Resulting u_int64_t hash
 *
 * @note Returns resulting hash, 0 otherwise.
 */
static uint64_t hash_key(const char* key) {
    if(!key)
        return 0;

    uint64_t hash = FNV_OFFSET;
    for (const char* p = key; *p; p++) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

/**
 * Inputs the key into the hash table
 *
 * Finds an open index of the hash table and sets the ht_entry.
 * Sets the index, key, and value. If value is already in the 
 * database, it returns the already inserted key.
 *
 * @param entries Entry to be entered into the hash table. 
 * @param capacity Capacity of the hash table.
 * @param key Member of the ht used to set and retrieve values.
 * @param value Retrieved value in the ht from the key.
 * @param plength How many items are in the table.
 * 
 * @return Key from new hash table entry. NULL on allocation error
 *
 * @warning Does not sanitize inputs.
 */
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

/**
 * Helper function for the ht_set_entry()
 *
 * @param table Hash table to insert value into.
 * @param key Key to insert into the hash table on.
 * @param value Value to be added into the table.
 * 
 * @return Returned value from ht_set_entry()
 *
 * @note Only sanitizes the hash table
 *
 * @see ht_set_entry()
 */
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

/**
 * Deallocates the hash table object.
 *
 * Frees all MIME objects and key extensions. Also
 * uses logic to make sure double frees don't occur. 
 *
 * @param table Hash table to free
 *
 * @note Only sanitizes the hash table
 */
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