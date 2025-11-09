#include "node.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

struct Node* init_tree()
{
    struct Node* head = NULL;

    system("find /home/remote/server/webpages -type f > results.txt");

    int fd = open("results.txt", O_RDONLY);
    if(fd < 0)
    {
        printf("find or reading failed\n");
        return NULL;
    }

    char buffer[READSIZE];
    int n = read(fd, buffer, sizeof(buffer));
    if(n < 0)
    {
        printf("Read failed in tree");
        return NULL;
    }

    char* tokptr;
    char *line = NULL;

    line = strtok_r(buffer, "\n", &tokptr);

    while(1)
    {
        line = strtok_r(NULL, "\n", &tokptr);

        if(!line)
            break;
    
        printf("line: %s\n", line);
        head = add_node(head, line);
    }
    printf("DONE\n");

    if(unlink("results.txt") < 0)
    {
	    printf("file cannot be deleted\n");
        return NULL;
    }
    printf("Deleted file\n");

    return head;
}

struct Node* add_node(struct Node* head, char* filename)
{
    if(!filename)
        return NULL;

    struct Node* new_node = malloc(sizeof(struct Node));
    if(!new_node)
    {
        printf("Failed to allocate memory\n");
        return NULL;
    }

    new_node->path = malloc(strlen(filename));
    strncpy(new_node->path, filename, strlen(filename));

    unsigned int path_hash = hashPath(filename);
    printf("filehash: %d\n", path_hash);
    if(path_hash == 0)
    {
        printf("pathhash failed\n");
        return NULL;
    }
    new_node->path_hash = path_hash;

    unsigned int file_hash = hashFile(filename);
    if(file_hash == 0)
    {
        printf("filehash failed\n");
        return NULL;
    }
    new_node->file_hash = file_hash;

    if(!head)
    {
        printf("Head failed\n");
        return new_node;
    }

    printf("Inserting node: %s", new_node->path);
    insert_node(head, new_node);

    return head;
}

int hashFile(char* filename)
{
    printf("Hashing file\n");
    if(!filename)
    {
        printf("Filename was NULL\n");
        return 0;
    }

    unsigned long hash = 5381;
    
    int fd = open(filename, O_RDONLY);
    if(fd < 0)
    {
        printf("Failed to open file\n");
        return 0;
    }

    char buffer[READSIZE];
    memset(buffer, 0, sizeof(buffer));

    while(1)
    {
        int n = read(fd, buffer, sizeof(buffer));
        if(n < 0)
        {
            printf("Read error\n");
            return 0;
        }
        else if(n == 0)
        {
            printf("EOF in filehash\n");
            break;
        }
        else
        {
            for(int i = 0; i < n; i++)
                hash += buffer[i];
        }
    }

    return hash;
}

int hashPath(char* filename)
{
    printf("Hashing path\n");
    if(!filename)
    {
        printf("Filename was NULL\n");
        return 0;
    }

    unsigned long hash = 5381;
    int c;

    while ((c = *filename++))
        hash = ((hash << 5) + hash) + c;

    return hash;
}

//Once I add the Merkle Tree, I'll change this to a struct Node* return value and return the balanced tree
void insert_node(struct Node* head, struct Node* new_node)
{
    struct Node* curr = head;
    while(curr)
    {
        if(curr->path_hash == new_node->path_hash)
        {
            printf("same key\n");
            return;
        }
        else if(curr->path_hash < new_node->path_hash)
        {
            if(!curr->left)
            {
                curr->left = new_node;
            }
            else
            {
                curr = curr->left;
            }
        }
        else
        {
            if(!curr->right)
            {
                curr->right = new_node;
            }
            else
            {
                curr = curr->right;
            }
        }
    }
    return;
}

void printTree(struct Node* curr, int level)
{
    if (curr == NULL)
        return;

    for (int i = 0; i < level; i++)
        printf(i == level - 1 ? "|-" : "  ");

    printf("%s: %d\n", curr->path, curr->path_hash);
    printTree(curr->left, level + 1);
    printTree(curr->right, level + 1);
}

unsigned int lookupNode(struct Node* head, unsigned int tag)
{
    if(!head)
    {
        printf("Node not initialized\n");
    }

    unsigned int hash = (unsigned int)tag;

    struct Node* curr = head;

    printf("Tag: %d from \n", hash);

    if(hash == 0)
    {
        printf("Hash < 0\n");
        return -1;
    }

    while(curr)
    {
        if(!curr)
        {
            return 0;
        }
        else if(curr->path_hash == hash)
        {
            printf("Returning -> %u\n", curr->file_hash);
            return curr->file_hash;
        }
        else if(curr->path_hash < hash)
        {
            curr = curr->left;
        }
        else
        {
            curr = curr->right;
        }
    }

    return 0;
}

void free_tree(struct Node* node)
{
    if (node != NULL)
    {
        free_tree(node->right);
        free(node->path);
        free_tree(node->left);
        free(node);
    }
}