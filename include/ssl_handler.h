#ifndef SSL_HANDLER_H
#define SSL_HANDLER_H

#include "types.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

void init_openssl(void);
void cleanup_openssl(void);
SSL_CTX* create_ssl_context(void);
void configure_ssl_context(SSL_CTX *ctx);
SSL* accept_ssl_connection(SSL_CTX* ctx, int client_fd);

#endif