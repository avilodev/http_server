#include "mime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Removes leading and trailing whitespace of a given string
 *
 * @param str String to be trimmed
 *
 * @return Successfully trimmed string
 *
 * @note Moves pointer in memory for beginning,
 * and adds null character at end.
 */
static char* trim(char* str) {
    if(!str)
        return NULL;

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

/**
 * Read mime file and create hash table.
 *
 * Opens the hash table file and reads lines of the file.
 * For every line that does not start with '#', add into hash table.
 * Return created hash table, NULL if error.
 *
 * @param filepath Filepath to turn into a hash table of mimes.
 *
 * @return New hash table that holds mimes.
 *
 * @note Returns NULL if filepath doesn't open or memory doesn't initialize.
 * @warning Must free entire hash table and mime_value attribute.
 *
 * @see init_hash(), trim()
 */
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
    
    return table;
}

/**
 * Looks up mime extension in hash table
 *
 * Verifies both parameters and sanitizes extension requested by client.
 * Then preforms a lookup in the hash table with the extension. Returns 
 * the extension type on successful lookup, application/octet-stream if error.
 *
 * @param table Hash table of mime values and extensions
 * @param extension Entire filepath requested by the client
 *
 * @return Mime extension if applicable, application/octet-stream otherwise
 *
 * @note Returns "application/octet-stream" if no value found or either param is NULL
 * @note Input sanitization occurs in the function, can pass through entire strings.
 *
 * @see ht_get()
 */
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

/**
 * Sanitization function for mime_get_type()
 *
 * @param table Hash table of mime values and extensions
 * @param extension Entire filepath requested by the client
 *
 * @return mime_get_type() function
 *
 * @note Returns "application/octet-stream" if no value found or either param is NULL
 * @note Input sanitization occurs in the function, can pass through entire strings.
 *
 * @see mime_get_type()
 */
const char* mime_get_type_from_filename(ht* table, const char* filename) {
    if (filename == NULL) {
        printf("filename is NULL\n");
        return "application/octet-stream";
    }

    // Find last dot in filename
    const char* ext = strrchr(filename, '.');
    
    if (ext == NULL) {
        return "application/octet-stream";
    }
    
    return mime_get_type(table, ext);
}