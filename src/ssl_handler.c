#include "ssl_handler.h"

/**
 * Initializes the OpenSSL library
 *
 * Performs one-time initialization of the OpenSSL library by loading error
 * strings, algorithms, and initializing the SSL subsystem. Must be called
 * once before any other OpenSSL functions.
 *
 * @note Should be called at program startup
 * @note Thread-safe in OpenSSL 1.1.0+, requires locking in earlier versions
 *
 * @see cleanup_openssl()
 */
void init_openssl() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

/**
 * Creates and initializes an SSL context
 *
 * Creates a new SSL_CTX structure for managing SSL/TLS connections. Uses
 * SSLv23_server_method() to support multiple protocol versions while
 * allowing negotiation with clients.
 *
 * @return Pointer to initialized SSL_CTX structure
 *
 * @note Exits program with error message if context creation fails
 * @warning Caller must free context with SSL_CTX_free() when done
 *
 * @see configure_ssl_context(), cleanup_openssl()
 */
SSL_CTX* create_ssl_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    return ctx;
}

/**
 * Configures SSL context with certificate and private key
 *
 * Loads the server's SSL certificate and private key from PEM files located
 * in the SERVER_PATH/keys/ directory. Validates that the private key matches
 * the public certificate to ensure proper SSL configuration.
 *
 * @param ctx Pointer to SSL_CTX structure to configure
 *
 * @note Expects cert.pem and key.pem in {SERVER_PATH}/keys/ directory
 * @note Exits program if certificate/key loading or validation fails
 *
 * @see create_ssl_context()
 */
void configure_ssl_context(SSL_CTX *ctx) {
    char cert_path[256] = {0};
    char key_path[256] = {0};

    sprintf(cert_path, "%s/keys/cert.pem", SERVER_PATH);
    sprintf(key_path, "%s/keys/key.pem", SERVER_PATH);

    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0) 
    {
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) 
    {
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if (!SSL_CTX_check_private_key(ctx)) 
    {
        fprintf(stderr, "Private key does not match the public certificate\n");
        exit(1);
    }  
}

/**
 * Cleans up OpenSSL library resources
 *
 * Frees OpenSSL library resources including error strings and algorithm
 * tables. Should be called once at program termination after all SSL
 * operations are complete.
 *
 * @note Should be called at program shutdown
 * @note Must be called after all SSL contexts and connections are freed
 *
 * @see init_openssl()
 */
void cleanup_openssl() {
    EVP_cleanup();
    ERR_free_strings();
}

/**
 * Accepts an SSL/TLS connection on a client socket
 *
 * Creates a new SSL structure for a client connection and performs the
 * SSL/TLS handshake. Binds the SSL structure to the provided socket file
 * descriptor and completes server-side handshake negotiation.
 *
 * @param ctx Pointer to configured SSL_CTX structure
 * @param client_fd File descriptor of accepted client socket
 *
 * @return Pointer to SSL structure on success, NULL on failure
 *
 * @note Prints OpenSSL errors to stderr if handshake fails
 * @warning Caller must free returned SSL structure with SSL_free()
 * @warning On failure, the SSL structure is automatically freed
 *
 * @see create_ssl_context(), configure_ssl_context()
 */
SSL* accept_ssl_connection(SSL_CTX* ctx, int client_fd) {
    SSL* ssl = SSL_new(ctx);
    if (!ssl) return NULL;
    
    SSL_set_fd(ssl, client_fd);
    
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }
    
    return ssl;
}