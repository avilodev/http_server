#ifndef CACHE_H
#define CACHE_H

#include "types.h"
#include "node.h"

// Cache operations
struct node_t* cache_lookup(struct node_t* tree_head, const char* path);
unsigned int cache_hash_path(const char* path);

// Cache tree management
struct node_t* cache_tree_init(const char* root_dir);
void cache_tree_free(struct node_t* tree_head);
void cache_tree_refresh(struct node_t** tree_head, const char* root_dir);

#endif /* CACHE_H */