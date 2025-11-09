#include "cache.h"
#include "logger.h"
#include <string.h>

/**
 * Looks for webpage node in the tree
 *
 * Validates that the head and path exists. If they do, hash the path
 * to an integer and call lookupNode with that value. Returns the Node
 * if found, NULL otherwise. 
 *
 * @param tree_head Head of the tree where the webpages are stored.
 * @param path Path where the file lies.
 *
 * @return Node that was found. If not found, the method returns NULL
 *
 * @note Validation function. Makes sure head and path exists and calls
 * lookupNode() from the Node.h file.
 *
 * @see lookupNode(), cache_hash_path()
 */
struct Node* cache_lookup(struct Node* tree_head, const char* path) {
    if (!tree_head || !path) return NULL;
    
    unsigned int hash = cache_hash_path(path);
    return lookupNode(tree_head, hash);
}

/**
 * Calls hashPath() from the Node function
 *
 * @param path Path where the file lies.
 *
 * @return Hash value for the path
 */
unsigned int cache_hash_path(const char* path) {
    if(!path) return 0;

    return hashPath(path);
}

/**
 * Calls the init_tree() in the node file.
 *
 * @param root_dir root directory of all files returned by the server.
 *
 * @return Head of the new tree contructed.
 */
struct Node* cache_tree_init(const char* root_dir) {
    log_message(LOG_INFO, "Initializing cache tree for: %s", root_dir);
    return init_tree();
}

/**
 * Validates the tree_head and calls the tree_free() function
 *
 * @param tree_head Tree head of the BST.
 */
void cache_tree_free(struct Node* tree_head) {
    if (tree_head) {
        free_tree(tree_head);
    }
}

/**
 * If a file updates, refreshing the tree updates cache info.
 *
 * @param tree_head Tree head of the BST.
 * @param tree_head root directory of all files returned by the server.
 */
void cache_tree_refresh(struct Node** tree_head, const char* root_dir) {
    if (!tree_head) return;
    
    log_message(LOG_INFO, "Refreshing cache tree");
    cache_tree_free(*tree_head);
    *tree_head = cache_tree_init(root_dir);
}