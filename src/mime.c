#include "mime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Trim leading and trailing whitespace
static char* trim(char* str) {
    // Trim leading
    while (isspace((unsigned char)*str)) {
        str++;
    }
    
    if (*str == '\0') {
        return str;
    }
    
    // Trim trailing
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    
    return str;
}

ht* mime_init(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (file == NULL) {
        perror("Error opening MIME types file");
        return NULL;
    }
    
    ht* table = init_hash();
    if (table == NULL) {
        fclose(file);
        return NULL;
    }
    
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    int line_count = 0;
    
    while ((read = getline(&line, &len, file)) != -1) {
        line_count++;
        
        // Remove trailing newline/carriage return
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
            line[--read] = '\0';
        }
        
        // Skip empty lines
        char* trimmed = trim(line);
        if (strlen(trimmed) == 0) {
            continue;
        }
        
        // Skip comments (lines starting with #)
        if (trimmed[0] == '#') {
            continue;
        }
        
        // Parse line: "video/mp4   mp4 mp4v mpg4"
        // MIME type is first token, extensions follow
        
        char mime_type[256];
        char extensions[512];
        
        // Use sscanf to split by whitespace
        int parsed = sscanf(trimmed, "%255s %511[^\n]", mime_type, extensions);
        
        if (parsed < 2) {
            // No extensions for this MIME type, skip
            continue;
        }
        
        // Duplicate MIME type string ONCE (will be shared by all extensions)
        char* mime_value = strdup(mime_type);
        if (mime_value == NULL) {
            continue;
        }
        
        // Split extensions by whitespace and add each to hash table
        char* saveptr = NULL;  // For thread-safe strtok_r
        char* ext = strtok_r(extensions, " \t", &saveptr);
        int ext_count = 0;
        
        while (ext != NULL) {
            // Add dot prefix if not present
            char ext_with_dot[128];
            if (ext[0] != '.') {
                snprintf(ext_with_dot, sizeof(ext_with_dot), ".%s", ext);
            } else {
                strncpy(ext_with_dot, ext, sizeof(ext_with_dot) - 1);
                ext_with_dot[sizeof(ext_with_dot) - 1] = '\0';
            }
            
            // Convert extension to lowercase
            for (char* p = ext_with_dot; *p; p++) {
                *p = tolower((unsigned char)*p);
            }
            
            // Insert into hash table (extension -> MIME type)
            // All extensions point to the same mime_value string
            const char* result = ht_set(table, ext_with_dot, mime_value);
            if (result != NULL) {
                ext_count++;
            }
            
            ext = strtok_r(NULL, " \t", &saveptr);
        }
        
        // If no extensions were added, free the mime_value
        if (ext_count == 0) {
            free(mime_value);
        }
    }
    
    free(line);
    fclose(file);
    
    printf("MIME: Loaded %d lines, %zu mappings\n", line_count, table->length);
    
    // Debug: Test a few lookups
    printf("MIME Test Lookups:\n");
    const char* test_html = (const char*)ht_get(table, ".html");
    const char* test_mp4 = (const char*)ht_get(table, ".mp4");
    const char* test_txt = (const char*)ht_get(table, ".txt");
    
    printf("  .html -> %s\n", test_html ? test_html : "NOT FOUND");
    printf("  .mp4 -> %s\n", test_mp4 ? test_mp4 : "NOT FOUND");
    printf("  .txt -> %s\n", test_txt ? test_txt : "NOT FOUND");
    
    return table;
}

const char* mime_get_type(ht* table, const char* extension) {
    if (table == NULL || extension == NULL) {
        return "application/octet-stream";
    }
    
    // Convert extension to lowercase
    char ext_lower[128];
    int i;
    for (i = 0; i < 127 && extension[i]; i++) {
        ext_lower[i] = tolower((unsigned char)extension[i]);
    }
    ext_lower[i] = '\0';
    
    // Ensure it starts with a dot
    char ext_with_dot[129];
    if (ext_lower[0] != '.') {
        snprintf(ext_with_dot, sizeof(ext_with_dot), ".%s", ext_lower);
    } else {
        strncpy(ext_with_dot, ext_lower, sizeof(ext_with_dot) - 1);
        ext_with_dot[sizeof(ext_with_dot) - 1] = '\0';
    }
    
    // Lookup in hash table
    const char* mime_type = (const char*)ht_get(table, ext_with_dot);
    
    if (mime_type != NULL) {
        return mime_type;
    }
    
    return "application/octet-stream";
}

const char* mime_get_type_from_filename(ht* table, const char* filename) {
    if (filename == NULL) {
        printf("filename is NULL\n");
        return "application/octet-stream";
    }
    
    printf("filename: %s\n", filename);

    // Find last dot in filename
    const char* ext = strrchr(filename, '.');
    
    if (ext == NULL) {
        return "application/octet-stream";
    }
    
    return mime_get_type(table, ext);
}