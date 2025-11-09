#include "node.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>


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
    int n = read(fd, buffer, sizeof(buffer) - 1);
    if(n < 0)
    {
        printf("Read failed in tree\n");
        return NULL;
    }
    buffer[n] = '\0';

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
    printf("Deleted results.txt\n");

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

    //printf("filename: %s\n", filename);
    new_node->path = strdup(filename);
    if (!new_node->path) 
    {
        printf("strdup failed\n");
        free(new_node);
        return NULL;
    }

    new_node->left = NULL;
    new_node->right = NULL;

    unsigned int path_hash = hashPath(filename);
    //printf("filehash: %d\n", path_hash);
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

    //printf("In main: %d\n", new_node->file_hash);
    //printf("Head: %d\n", new_node->file_hash);


    new_node->last_modified = update_last_modified(filename);

    if(head == NULL)
    {
        printf("Head failed\n");
        return new_node;
    }

    //printf("Before inserting node\n");
    //printf("Inserting node: %s", new_node->path);
    insert_node(head, new_node);

    return head;
}

int hashFile(char* filename)
{
    //printf("Hashing file\n");
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

    //printf("hash in filehash %ld\n", hash);
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

//Mon, 28 Apr 2025 19:18:33 GMT
char* update_last_modified(char* filename)
{
    struct stat file_stat;
    printf("Filename: %s\n", filename);
    if (stat(filename, &file_stat) == 0) 
    {
        printf("inside loop thing: %s\n", filename);
        char* time_buf = malloc(READSIZE);
        if (time_buf == NULL) 
            return NULL;
        
        struct tm *tm_info = gmtime(&file_stat.st_mtime);
        strftime(time_buf, READSIZE, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
        
        return time_buf;
    }
    return NULL;
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
        else if(curr->path_hash > new_node->path_hash)
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

    printf("%s: %u\n", curr->path, curr->path_hash);
    printTree(curr->left, level + 1);
    printTree(curr->right, level + 1);
}

struct Node* lookupNode(struct Node* head, unsigned int tag)
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
        return NULL;
    }

    printf("going into hash \n");

    while(curr)
    {
        if(!curr)
        {
            printf("retruning 0\n");
            return NULL;
        }
        else if(curr->path_hash == hash)
        {
            printf("Returning -> %u\n", curr->file_hash);
            return curr;
        }
        else if(curr->path_hash > hash)
        {
            curr = curr->left;
        }
        else
        {
            curr = curr->right;
        }
    }

    return NULL;
}

void free_tree(struct Node* node)
{
    if (node != NULL)
    {
        free_tree(node->right);
        free(node->path);
        free(node->last_modified);
        free_tree(node->left);
        free(node);
    }
}