#include "post.h"
#include "session.h"
#include "utils.h"
#include <pthread.h>

// External reference to the global database connection (defined in main.c)
extern sqlite3* g_database;
extern pthread_mutex_t g_db_mutex;

// Function to initialize the database and create users table
int init_database(sqlite3** db)
{
    int rc;
    char* err_msg = 0;
    
    rc = sqlite3_open(SERVER_PATH "/var/db/users.db", db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(*db));
        return rc;
    }
    
    // Password field now stores the hashed password (text, not uint64_t)
    // crypto_pwhash_STRBYTES is 128 bytes - includes hash + salt + parameters
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS users("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL UNIQUE, "
        "password_hash TEXT NOT NULL);";
    
    rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }
    
    //printf("Database initialized successfully\n");
    return SQLITE_OK;
}

// Function to hash a password securely
// Returns: 0 on success, -1 on failure
int hash_password(const char* password, char* hashed_output)
{
    if (crypto_pwhash_str(
            hashed_output,
            password,
            strlen(password),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE
        ) != 0) {
        fprintf(stderr, "Password hashing failed (out of memory)\n");
        return -1;
    }
    
    return 0;
}

// Function to verify a password against a stored hash
// Returns: 1 if password matches, 0 if not
int verify_password(const char* hashed, const char* password)
{
    if (crypto_pwhash_str_verify(
            hashed,
            password,
            strlen(password)
        ) == 0) {
        return 1;
    }
    
    return 0;
}

// Function to check if username and password match a record in database
int verify_user(sqlite3* db, const char* username, const char* password)
{
    sqlite3_stmt* stmt;
    int rc;
    int result = 0;
    
    const char* sql = "SELECT password_hash FROM users WHERE username = ?;";

    pthread_mutex_lock(&g_db_mutex);
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        const unsigned char* stored_hash = sqlite3_column_text(stmt, 0);
        result = verify_password((const char*)stored_hash, password);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return result;
}

// Function to add a new user to the database
int add_user(sqlite3* db, const char* username, const char* password)
{
    sqlite3_stmt* stmt;
    int rc;
    
    char hashed_password[crypto_pwhash_STRBYTES];
    
    if (hash_password(password, hashed_password) != 0) {
        fprintf(stderr, "Failed to hash password\n");
        return -1;
    }
    
    //printf("Original password: %s\n", password);
    //printf("Hashed password: %s\n", hashed_password);
    //printf("(Notice the hash includes salt and algorithm parameters)\n");
    
    const char* sql = "INSERT INTO users(username, password_hash) VALUES(?, ?);";

    pthread_mutex_lock(&g_db_mutex);
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&g_db_mutex);
        return rc;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hashed_password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    
    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            log_message(LOG_INFO, "Username already exists: %s", username);
        } else {
            log_message(LOG_INFO, "Execution failed for user: %s", username);
        }
        return rc;
    }
    
    //printf("User added successfully: %s\n", username);
    return SQLITE_OK;
}

void handle_post(Client* client)
{
    /* Reject missing or oversized bodies before touching any content. */
    if (client->content_length < 0) {
        send_error_response(411, client);  /* 411 Length Required */
        return;
    }
    if (client->content_length > MAX_BODY_SIZE) {
        send_error_response(413, client);  /* 413 Content Too Large */
        return;
    }

    if (client->post_type) {
        if (strncmp(client->post_type, "application/x-www-form-urlencoded", 33) == 0) {
            handle_post_form_urlencoded(client);
        }
    }
}

void handle_post_form_urlencoded(Client* client)
{
    // Use the global database connection from main.c
    if (!g_database) {
        //printf("Database not initialized\n");
        send_error_response(500, client);
        return;
    }
    
    char* action = strstr(client->body, "action=");
    
    if (action) {
        action += 7;
        
        if (strncmp(action, "register", 8) == 0) {
            handle_registration(g_database, client);
        }
        else if (strncmp(action, "login", 5) == 0) {
            handle_login(g_database, client);
        }
        else {
            //printf("Unknown action\n");
            send_error_response(400, client);
        }
    }
    else if (client->path && strstr(client->path, "/register")) {
        handle_registration(g_database, client);
    }
    else if (client->path && strstr(client->path, "/login")) {
        handle_login(g_database, client);
    }
    else {
        handle_login(g_database, client);
    }
    
    // DON'T close the database here - it's global and managed in main()
}

// Separate function to handle user registration
void handle_registration(sqlite3* db, Client* client)
{
    url_encoded* creds = parse_url_encoded(client);
    
    if (!creds || !creds->username || !creds->password) {
        //printf("Invalid registration data\n");
        send_error_response(400, client);
        if (creds) {
            if (creds->username) free(creds->username);
            if (creds->password) free(creds->password);
            free(creds);
        }
        return;
    }
    
    if (strlen(creds->username) < 3) {
        //printf("Username too short (minimum 3 characters)\n");
        send_error_response(400, client);
        free(creds->username);
        free(creds->password);
        free(creds);
        return;
    }
    
    if (strlen(creds->password) < 8) {
        //printf("Password too short (minimum 8 characters)\n");
        send_error_response(400, client);
        free(creds->username);
        free(creds->password);
        free(creds);
        return;
    }
    
    int result = add_user(db, creds->username, creds->password);

    log_message(LOG_INFO, "New user created: %s", creds->username);
    
    if (result == SQLITE_OK) {
        //printf("✓ Registration successful for user: %s\n", creds->username);
        // Send login page
        send_redirect_response("/login.html", client);
    } else if (result == SQLITE_CONSTRAINT) {
        //printf("✗ Username already exists: %s\n", creds->username);
        send_error_response(409, client);  // 409 Conflict
    } else {
        //printf("✗ Registration failed for user: %s\n", creds->username);
        send_error_response(500, client);
    }
    
    free(creds->username);
    free(creds->password);
    free(creds);
}

// Separate function to handle user login
void handle_login(sqlite3* db, Client* client)
{
    url_encoded* creds = parse_url_encoded(client);
    
    if (!creds || !creds->username || !creds->password) {
        //printf("Invalid credentials format\n");
        send_error_response(400, client);
        if (creds) {
            if (creds->username) free(creds->username);
            if (creds->password) free(creds->password);
            free(creds);
        }
        return;
    }
    
    if (verify_user(db, creds->username, creds->password)) {
        log_message(LOG_INFO, "Successful user login: %s", creds->username);

        char* token = session_create(creds->username);
        if (token) {
            send_login_redirect("/landing.html", token, SESSION_EXPIRY_SEC, client);
            free(token);
        } else {
            send_error_response(500, client);
        }
    } else {
        log_message(LOG_INFO, "Failed user login: %s", creds->username);

        send_error_response(401, client);  // 401 Unauthorized
    }
    
    free(creds->username);
    free(creds->password);
    free(creds);
}

url_encoded* parse_url_encoded(Client* client)
{
    url_encoded* creds = malloc(sizeof(url_encoded));
    
    if (!creds) return NULL;
    
    creds->username = NULL;
    creds->password = NULL;

    /* Extract and URL-decode username */
    char* user_start = strstr(client->body, "username=");
    if (!user_start) { free(creds); return NULL; }
    user_start += 9;

    char* user_end = strchr(user_start, '&');
    size_t user_raw_len = user_end ? (size_t)(user_end - user_start) : strlen(user_start);
    char* user_raw = strndup(user_start, user_raw_len);
    if (!user_raw) { free(creds); return NULL; }
    creds->username = malloc(user_raw_len + 1);
    if (creds->username) url_decode(creds->username, user_raw, user_raw_len + 1);
    free(user_raw);

    /* Extract and URL-decode password */
    char* pass_start = strstr(client->body, "password=");
    if (!pass_start) {
        free(creds->username);
        free(creds);
        return NULL;
    }
    pass_start += 9;

    char* pass_end = strchr(pass_start, '&');
    size_t pass_raw_len = pass_end ? (size_t)(pass_end - pass_start) : strlen(pass_start);
    char* pass_raw = strndup(pass_start, pass_raw_len);
    if (!pass_raw) { free(creds->username); free(creds); return NULL; }
    creds->password = malloc(pass_raw_len + 1);
    if (creds->password) url_decode(creds->password, pass_raw, pass_raw_len + 1);
    free(pass_raw);

    return creds;
}