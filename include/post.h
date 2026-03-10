#ifndef POST_H
#define POST_H

#include "types.h"
#include "response.h"
#include "logger.h"

#include <sqlite3.h>
#include <sodium.h>

typedef struct URL_ENCODED
{
    char* username;
    char* password;
}url_encoded;

int init_database(sqlite3** db);
int hash_password(const char* password, char* hashed_output);
int verify_password(const char* hashed, const char* password);
int verify_user(sqlite3* db, const char* username, const char* password);
int add_user(sqlite3* db, const char* username, const char* password);

void handle_post(Client* client);
void handle_post_form_urlencoded(Client* client);
void handle_registration(sqlite3* db, Client* client);
void handle_login(sqlite3* db, Client* client);

url_encoded* parse_url_encoded(Client* client);


#endif // POST_H