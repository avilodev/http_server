#ifndef CACHE_H
#define CACHE_H

#include "types.h"
#include "node.h"

// Cache operations
struct Node* cache_lookup(struct Node* tree_head, const char* path);
unsigned int cache_hash_path(const char* path);

// Cache tree management
struct Node* cache_tree_init(const char* root_dir);
void cache_tree_free(struct Node* tree_head);
void cache_tree_refresh(struct Node** tree_head, const char* root_dir);

#endif