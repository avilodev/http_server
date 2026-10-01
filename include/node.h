#ifndef NODE_H
#define NODE_H

#include "types.h"

#define READSIZE 4096

struct node_t {
	char* path;
 
	unsigned int path_hash;
	unsigned int file_hash;

	char* last_modified;

	struct node_t* left;
	struct node_t* right;
};

struct node_t* init_tree();
struct node_t* add_node(struct node_t*, char*);
int hash_file(char* filename);
int hash_path(const char* filename);
char* update_last_modified(char*);
int insert_node(struct node_t*, struct node_t*);
struct node_t* lookup_node(struct node_t*, unsigned int);
void print_tree(struct node_t*, int);
void free_tree(struct node_t*);

#endif /* NODE_H */
