#include "node.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

/**
 * Creates and initializes a BST from files in the webroot directory
 *
 * Executes a system find command to locate all files in the webpages
 * directory, then creates a BST node for each discovered file. Files
 * in the /videos/ subdirectory are excluded from the tree. Each node
 * contains the file path, path hash, content hash, and last modified time.
 *
 * @return Pointer to root node of the created BST, or NULL on error
 *
 * @note Excludes files containing "/videos/" in their path
 * @note Creates and removes temporary "results.txt" file during execution
 * @warning Caller must free the tree with free_tree() when done
 *
 * @see add_node(), free_tree()
 */
struct Node* init_tree() {
    struct Node* head = NULL;

    // Build find command to locate all files in webpages directory
    char* webpages_dir = malloc(READSIZE);
    if (!webpages_dir) {
        fprintf(stderr, "Failed to allocate memory for find command\n");
        return NULL;
    }
    
    sprintf(webpages_dir, "find %s/webpages -type f > results.txt", SERVER_PATH);
    
    int ret = system(webpages_dir);
    if (ret != 0) {
        fprintf(stderr, "Warning: system('%s') failed with code %d\n", webpages_dir, ret);
    }
    free(webpages_dir);

    // Read the results file
    int fd = open("results.txt", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open results.txt\n");
        return NULL; 
    }

    char buffer[READSIZE];
    int n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    
    if (n < 0) {
        fprintf(stderr, "Read failed in tree initialization\n");
        return NULL;
    }
    buffer[n] = '\0';

    // Parse file paths and build tree
    char* tokptr;
    char* line = strtok_r(buffer, "\n", &tokptr);
    
    if (line) {
        head = add_node(head, line);
    }

    while ((line = strtok_r(NULL, "\n", &tokptr)) != NULL) {
        // Skip video files
        if (strstr(line, "/videos/")) {
            continue;
        }
        
        head = add_node(head, line);
    }
    
    // Clean up temporary file
    if (unlink("results.txt") < 0) {
        fprintf(stderr, "Warning: Could not delete results.txt\n");
    }

    return head;
}

/**
 * Creates a new BST node from a file path
 *
 * Allocates and initializes a new node containing the file path, path hash,
 * content hash, and last modified timestamp. If the tree is non-empty,
 * inserts the node into the appropriate position in the BST based on
 * path hash comparison.
 *
 * @param head Root node of the current BST, or NULL for new tree
 * @param filename Full file path for the new node
 *
 * @return Root of the tree (unchanged if tree exists, new node otherwise)
 *         Returns NULL on allocation failure or invalid filename
 *
 * @note Path hash and file hash must be non-zero for successful insertion
 * @warning Caller must eventually free tree with free_tree()
 *
 * @see hashPath(), hashFile(), update_last_modified(), insert_node()
 */
struct Node* add_node(struct Node* head, char* filename) {
    if (!filename) {
        return NULL;
    }

    // Allocate new node
    struct Node* new_node = malloc(sizeof(struct Node));
    if (!new_node) {
        fprintf(stderr, "Failed to allocate memory for node\n");
        return NULL;
    }

    // Duplicate file path
    new_node->path = strdup(filename);
    if (!new_node->path) {
        free(new_node);
        return NULL;
    }

    new_node->left = NULL;
    new_node->right = NULL;

    // Calculate path hash
    unsigned int path_hash = hashPath(filename);
    if (path_hash == 0) {
        free(new_node->path);
        free(new_node);
        return NULL;
    }
    new_node->path_hash = path_hash;

    // Calculate file content hash
    unsigned int file_hash = hashFile(filename);
    if (file_hash == 0) {
        free(new_node->path);
        free(new_node);
        return NULL;
    }
    new_node->file_hash = file_hash;
    
    // Get last modified timestamp
    new_node->last_modified = update_last_modified(filename);

    // If tree is empty, return new node as root
    if (head == NULL) {
        return new_node;
    }

    // Insert into existing tree
    insert_node(head, new_node);
    return head;
}

/**
 * Computes hash of file contents
 *
 * Opens and reads the entire file, computing a cumulative hash by adding
 * each byte value to a running total. Uses a simple additive hashing
 * algorithm starting with seed value 5381.
 *
 * @param filename Path to file to hash
 *
 * @return Hash value of file contents, or 0 on error
 *
 * @note Returns 0 if filename is NULL or file cannot be opened
 * @note Uses simple additive hash (not cryptographically secure)
 *
 * @see add_node()
 */
int hashFile(char* filename) {
    if (!filename) {
        return 0;
    }

    unsigned long hash = 5381;
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    char buffer[READSIZE];
    ssize_t n;

    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < n; i++) {
            hash += buffer[i];
        }
    }
    
    close(fd);

    if (n < 0) {
        return 0;
    }

    return hash;
}

/**
 * Computes hash of file path string
 *
 * Uses the djb2 hashing algorithm to generate a hash value from the
 * file path string. Iterates through each character, combining it
 * with the running hash using bit shifts and addition.
 *
 * @param filename File path string to hash
 *
 * @return Hash value of the path, or 0 if filename is NULL
 *
 * @note Uses djb2 algorithm: hash = hash * 33 + c
 * @note Not cryptographically secure, intended for BST indexing
 *
 * @see add_node(), lookupNode()
 */
int hashPath(const char* filename) {
    if (!filename) {
        return 0;
    }

    unsigned long hash = 5381;
    int c;

    while ((c = *filename++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }

    return hash;
}

/**
 * Retrieves last modification time of file
 *
 * Uses stat() to get file metadata and formats the modification time
 * as an HTTP-date string (RFC 7231 format). Returns dynamically allocated
 * string in GMT timezone.
 *
 * @param filename Path to file to check
 *
 * @return Allocated string with formatted date, or NULL on error
 *
 * @note Returns NULL if filename is NULL, stat() fails, or allocation fails
 * @note Format: "Day, DD Mon YYYY HH:MM:SS GMT"
 * @warning Caller must free the returned string
 *
 * @see add_node()
 */
char* update_last_modified(char* filename) {
    if (!filename) {
        return NULL;
    }

    struct stat file_stat;
    if (stat(filename, &file_stat) != 0) {
        return NULL;
    }

    char* time_buf = malloc(READSIZE);
    if (!time_buf) {
        return NULL;
    }
    
    struct tm* tm_info = gmtime(&file_stat.st_mtime);
    strftime(time_buf, READSIZE, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    return time_buf;
}

/**
 * Inserts a node into the BST using iterative approach
 *
 * Traverses the tree iteratively to find the correct insertion point
 * based on path hash comparison. If a node with the same path hash
 * already exists, the insertion is skipped (no duplicates allowed).
 *
 * @param head Root node of the BST (must not be NULL)
 * @param new_node Node to insert into the tree
 *
 * @note Does nothing if duplicate path hash is found
 * @note Uses path_hash for comparison: left if less, right if greater
 *
 * @see add_node()
 */
void insert_node(struct Node* head, struct Node* new_node) {
    struct Node* curr = head;
    
    while (curr) {
        if (curr->path_hash == new_node->path_hash) {
            // Duplicate found, don't insert
            return;
        } else if (curr->path_hash > new_node->path_hash) {
            if (!curr->left) {
                curr->left = new_node;
                return;
            }
            curr = curr->left;
        } else {
            if (!curr->right) {
                curr->right = new_node;
                return;
            }
            curr = curr->right;
        }
    }
}

/**
 * Recursively prints tree structure with indentation
 *
 * Performs an in-order traversal of the BST, printing each node's path
 * and hash value with visual indentation to show tree structure. Uses
 * "|-" prefix for tree branches.
 *
 * @param curr Current node to print (use root to print entire tree)
 * @param level Current depth level in tree (use 0 for root)
 *
 * @note Prints in-order: left subtree, current node, right subtree
 * @note Visual indentation shows tree structure
 *
 * @see init_tree()
 */
void printTree(struct Node* curr, int level) {
    if (curr == NULL) {
        return;
    }

    // Print indentation
    for (int i = 0; i < level; i++) {
        printf(i == level - 1 ? "|-" : "  ");
    }

    printf("%s: %u\n", curr->path, curr->path_hash);
    
    // Recursively print children
    printTree(curr->left, level + 1);
    printTree(curr->right, level + 1);
}

/**
 * Searches BST for node with matching path hash
 *
 * Performs iterative binary search through the BST to find a node
 * with the specified path hash. Compares hash values at each node
 * to determine whether to search left or right subtree.
 *
 * @param head Root node of the BST
 * @param tag Path hash value to search for
 *
 * @return Pointer to matching node, or NULL if not found
 *
 * @note Returns NULL if head is NULL or tag is 0
 * @note Uses binary search: O(log n) average, O(n) worst case
 *
 * @see hashPath(), add_node()
 */
struct Node* lookupNode(struct Node* head, unsigned int tag) {
    if (!head || tag == 0) {
        return NULL;
    }

    struct Node* curr = head;

    while (curr) {
        if (curr->path_hash == tag) {
            return curr;
        } else if (curr->path_hash > tag) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }

    return NULL;
}

/**
 * Recursively frees entire BST and all node data
 *
 * Performs post-order traversal to free all nodes in the tree along
 * with their dynamically allocated path and last_modified strings.
 * Frees children before parent to avoid accessing freed memory.
 *
 * @param node Root node of tree/subtree to free
 *
 * @note Safe to call with NULL node (no-op)
 * @note Frees in post-order: left, right, then current node
 * @warning After calling, all node pointers in tree are invalid
 *
 * @see init_tree(), add_node()
 */
void free_tree(struct Node* node) {
    if (node == NULL) {
        return;
    }

    // Post-order traversal: free children first
    free_tree(node->left);
    free_tree(node->right);
    
    // Free node data
    free(node->path);
    free(node->last_modified);
    free(node);
}