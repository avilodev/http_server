#ifndef NODE_H
#define NODE_H

#define READSIZE 4096

struct Node {
    char* path;

    unsigned int path_hash;
    unsigned int file_hash;

    char* last_modified;

    struct Node* left;
    struct Node* right;
};

struct Node* init_tree();
struct Node* add_node(struct Node*, char*);
int hashFile(char* filename);
int hashPath(char* filename);
char* update_last_modified(char*);
void insert_node(struct Node*, struct Node*);
struct Node* lookupNode(struct Node*, unsigned int);
void printTree(struct Node*, int);
void free_tree(struct Node*);

#endif  