#include "server.h"

//Make head a global that was i can free it easy and dont have to pass it in anywhere.
volatile sig_atomic_t SIGNAL_FLAG = 0;
void signal_handler(int signum) 
{
    (void) signum;
    printf("Signal Flag Recieved\n");
    SIGNAL_FLAG = 1;
}

volatile sig_atomic_t TREE_FLAG = 0;
void updateTree(int signum)
{
    (void) signum;
    printf("Tree Flag Recieved\n");
    TREE_FLAG = 1;
}


void cleanup_openssl() {
    EVP_cleanup();
    ERR_free_strings();
}


void init_openssl() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}


SSL_CTX* create_ssl_context() 
{
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

void configure_ssl_context(SSL_CTX *ctx) 
{

    if (SSL_CTX_use_certificate_file(ctx, "/home/remote/server/keys/cert.pem", SSL_FILETYPE_PEM) <= 0) 
    {
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "/home/remote/server/keys/key.pem", SSL_FILETYPE_PEM) <= 0) 
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

int main(int argc, char** argv)
{ 
    if(argc != 1)
    {
        printf("Usage <sudo ./server>\n");
        exit(1);
    } 

    signal(SIGINT, updateTree);           //refresh tree
    signal(SIGTERM, signal_handler);      //quit program
    signal(SIGQUIT, signal_handler);      //quit program
    //signal(SIGUSR1, updateTree);  

    (void) argv;

    struct Node* tree_head = init_tree();
    struct Node* curr = tree_head;
    printTree(curr, 0);

    init_openssl();
    SSL_CTX *ssl_ctx = create_ssl_context();
    configure_ssl_context(ssl_ctx);

    //Testing only
    //SSL_CTX *ssl_ctx = NULL;


    int http_sock, https_sock;
    struct sockaddr_in http_server_addr, https_server_addr;
    socklen_t http_server_len = sizeof(http_server_addr);
    socklen_t https_server_len = sizeof(https_server_addr); 

    //http socket
    if((http_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Unable to create socket 80.\n");
        exit(1);
    }

    if((https_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Unable to create socket 443.\n");
        exit(1);
    }

    bzero(&http_server_addr, sizeof(http_server_addr));
    http_server_addr.sin_family = AF_INET;
    http_server_addr.sin_addr.s_addr = INADDR_ANY;
    http_server_addr.sin_port = htons(HTTP_PORT);

    bzero(&https_server_addr, sizeof(https_server_addr));
    https_server_addr.sin_family = AF_INET;
    https_server_addr.sin_addr.s_addr = INADDR_ANY;
    https_server_addr.sin_port = htons(HTTPS_PORT);

    int opt = 1;
    if (setsockopt(http_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) 
    {
        printf("SO_REUSEADDR failed");
        close(http_sock);
        close(https_sock);
        exit(1);
    }

    if (setsockopt(https_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) 
    {
        printf("SO_REUSEADDR failed");
        close(http_sock);
        close(https_sock);
        exit(1);
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(http_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) 
    {
        printf("SO_RCVTIMEO failed");
        close(http_sock);
        close(https_sock);
        exit(1);
    }

    if (setsockopt(https_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) 
    {
        printf("SO_RCVTIMEO failed");
        close(http_sock);
        close(https_sock);
        exit(1);
    }

    if(bind(http_sock, (struct sockaddr*)&http_server_addr, http_server_len) < 0)
    {
        printf("Bind failed to port %d\n", HTTP_PORT);
        
        close(http_sock);
        close(https_sock);
        exit(1);
    }

    if(bind(https_sock, (struct sockaddr*)&https_server_addr, https_server_len) < 0)
    {
        printf("Bind failed to port %d\n", HTTPS_PORT);
        
        close(http_sock);
        close(https_sock);
        exit(1);
    }

    printf("Successfully opened on port %d\n", HTTP_PORT);
    printf("Successfully opened on port %d\n", HTTPS_PORT);

    if(listen(http_sock, BACKLOG) < 0)
    {
        printf("Listen failed 80\n");

        close(http_sock);
        close(https_sock);
        exit(1);
    }

    if(listen(https_sock, BACKLOG) < 0)
    {
        printf("Listen failed 443\n");

        fprintf(stderr, "listen failed (errno %d): %s\n", errno, strerror(errno));

        close(http_sock);
        close(https_sock);
        exit(1);
    }

    fd_set http_read_fds;
    int http_client_sockets[MAX_CLIENTS] = {0};

    fd_set https_read_fds;
    int https_client_sockets[MAX_CLIENTS] = {0};
    SSL *https_client_ssl[MAX_CLIENTS] = {NULL};

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);  

    while(SIGNAL_FLAG == 0)
    {
        //If file was modified & reset signal recieved
        if(TREE_FLAG == 1)  
        {
            free_tree(tree_head); 
            printf("Freed tree\n");

            tree_head->left = NULL;
            tree_head->right = NULL;
            tree_head = NULL; 

            tree_head = init_tree();
            curr = tree_head;
            printTree(curr, 0);

            TREE_FLAG = 0;
        } 

        FD_ZERO(&http_read_fds);
        FD_SET(http_sock, &http_read_fds);

        FD_ZERO(&https_read_fds);
        FD_SET(https_sock, &https_read_fds);
  
        int reuse_http_sock = http_sock;
        int reuse_https_sock = https_sock;

        for (int i = 0; i < MAX_CLIENTS; i++) 
        {
            if (http_client_sockets[i] > 0) {
                FD_SET(http_client_sockets[i], &http_read_fds);
            }

            if (https_client_sockets[i] > 0) {
                FD_SET(https_client_sockets[i], &https_read_fds);
            }
            
            if (http_client_sockets[i] > reuse_http_sock) {
                reuse_http_sock = http_client_sockets[i]; 
            }

            if (https_client_sockets[i] > reuse_https_sock) {
                reuse_https_sock = https_client_sockets[i];
            }
        } 

        int http_activity = select(reuse_http_sock + 1, &http_read_fds, NULL, NULL, &tv);
        if (http_activity < 0 && errno != EINTR) 
        {
            continue;
        }

        int https_activity = select(reuse_https_sock + 1, &https_read_fds, NULL, NULL, &tv);
        if (https_activity < 0 && errno != EINTR) 
        {
            continue;
        }

        int client_socket;

        //http socket handling
        if (FD_ISSET(http_sock, &http_read_fds)) 
        {
            if ((client_socket = accept(http_sock, (struct sockaddr *)&client_addr, &addr_len)) < 0) 
            {
                continue;
            }
            
            printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            for (int i = 0; i < MAX_CLIENTS; i++) 
            {
                if (http_client_sockets[i] == 0)
                {
                    http_client_sockets[i] = client_socket;
                    printf("Adding to list of sockets as %d\n", i);
                    break;
                }
                else
                {
                    printf("Connection in %d\n", i);
                }
            }
        }

        char request[MAXLINE];
        for (int i = 0; i < MAX_CLIENTS; i++) 
        {
            if (http_client_sockets[i] > 0 && FD_ISSET(http_client_sockets[i], &http_read_fds))
            {
                printf("Going into request for index %d\n", i);
                memset(request, 0, MAXLINE);
                
                ssize_t recv_len;
                if ((recv_len = recv(http_client_sockets[i], request, sizeof(request), 0)) == 0) 
                {
                    getpeername(http_client_sockets[i], (struct sockaddr*)&client_addr, &addr_len);
                    printf("Client disconnected: %s:%d\n", 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                    continue;
                }

                if(recv_len <= 0)
                {
                    printf("Recieved nothing\n");
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                    continue;
                }
                else if(recv_len > (ssize_t)sizeof(request) - 1)
                {
                    printf("Length Exceeded\n");
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                    continue;
                }

                request[recv_len] = '\0';
                printf("Recieved %d\n", recv_len);	
                printf("Received from client %d: \n%s", i, request);


                struct Client* client = init_request(request, http_client_sockets[i], false);
                if(client == NULL)
                {
                    printf("Failed to initialized client\n");
                    free(client);
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                    continue;
                }

                //HTTP/1.1 301 Moved Permanently
                //Location: https://100.64.18.186/
                if(client->upgrade_tls)
                {   
                    char headers[MAX_RESPONSE];
                    int header_len = 0;
                    memset(headers, 0, sizeof(headers));

                    header_len += sprintf(headers + header_len, "%s 301 Moved Permanently\r\n", client->version);
                    header_len += sprintf(headers + header_len, "Location: https://%s%s\r\n\r\n", IP_REDIRECT, client->path);
                    

                    if(send(http_client_sockets[i], headers, header_len, 0) < 0)
                    {
                        printf("Error sending http response.\n");
                    }

                    printf("Response:\n%s\n", headers);
                    master_log(301, client);
                    free(client);
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;

                    continue;
                }
                
                client->client_fd = http_client_sockets[i];
                client->client_ip = inet_ntoa(client_addr.sin_addr);
                client->client_port = ntohs(client_addr.sin_port);

                client->fd = process_request(client, tree_head);
                if(client->fd < 0)
                {
                    printf("Could not process request\n");  
                    free(client); 
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;        
                    continue;  
                }   
                else if(client->fd == 1) //Cached Response
                {   
                    printf("Cached Response\n");  
                    free(client->full_path);
                    free(client);
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;  
                    continue;
                }

                if(strncmp(client->method, "GET", 3) == 0 || strncmp(client->method, "HEAD", 4) == 0 || strncmp(client->method, "OPTIONS", 7) == 0 || strncmp(client->method, "TRACE", 5) == 0)
                {
                    if(send_response(tree_head, client) < 0)
                    {
                        send_code(406, client->client_fd);
                        master_log(406, client);
                        printf("Does not accept filetype(s) html, svg, jpeg\n");
                        free(client->full_path);
                        free(client);
                        close(http_client_sockets[i]);
                        http_client_sockets[i] = 0;
                        continue;

                    }
                }
                else if(strncmp(client->method, "POST", 4) == 0 || strncmp(client->method, "PUT", 3) == 0 || strncmp(client->method, "DELETE", 6) == 0 || strncmp(client->method, "CONNECT", 7) == 0 || strncmp(client->method, "PATCH", 5) == 0)
                {
                    send_code(501, client->client_fd);
                    master_log(501, client);
                    printf("Methods not Implemented.\n");
                    free(client->full_path);
                    free(client);
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                    continue;
                }
                else
                {
                    send_code(501, client->client_fd);
                    master_log(501, client);
                    printf("Methods not Implemented\n");
                    free(client->full_path);
                    free(client);
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                    continue;
                }

                if(!client->connection_status)
                {
                    printf("Closed\n");
                    close(http_client_sockets[i]);
                    http_client_sockets[i] = 0;
                }

                free(client->full_path);
                free(client);
            }
        }

        client_socket = 0;
        if (FD_ISSET(https_sock, &https_read_fds)) 
        {
            if ((client_socket = accept(https_sock, (struct sockaddr *)&client_addr, &addr_len)) < 0) 
            {
                continue;
            }
            
            printf("New https connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) 
            {
                if (https_client_sockets[i] == 0)
                {
                    slot = i;
                    break;
                }
            }
            
            if (slot == -1) {
                close(client_socket);
                continue;
            }
            
            SSL *ssl = SSL_new(ssl_ctx);
            if (!ssl) {
                fprintf(stderr, "Failed to create SSL structure\n");
                close(client_socket);
                continue;
            }
            
            if (SSL_set_fd(ssl, client_socket) != 1) {
                fprintf(stderr, "Failed to attach SSL to socket\n");
                SSL_free(ssl);
                close(client_socket);
                continue;
            }
            
            int res = SSL_accept(ssl);
            if (res <= 0) {
                fprintf(stderr, "SSL handshake failed\n");
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                close(client_socket);
                continue;
            }
            
            https_client_sockets[slot] = client_socket;
            https_client_ssl[slot] = ssl;
            printf("Adding to list of https sockets as %d\n", slot);
        }

        //starting https requests
        memset(request, 0, sizeof(request));
        for (int i = 0; i < MAX_CLIENTS; i++) 
        {
            if (https_client_sockets[i] > 0 && FD_ISSET(https_client_sockets[i], &https_read_fds))
            {
                printf("Going into https request for index %d\n", i);
                memset(request, 0, MAXLINE);

                SSL *ssl = https_client_ssl[i];
                ssize_t recv_len = SSL_read(ssl, request, sizeof(request) - 1);
                if (recv_len <= 0) 
                {
                    int err = SSL_get_error(ssl, recv_len);
                    if (err == SSL_ERROR_ZERO_RETURN) {
                        getpeername(https_client_sockets[i], (struct sockaddr*)&client_addr, &addr_len);
                        printf("HTTPS Client disconnected: %s:%d\n", 
                               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    } else {
                        fprintf(stderr, "SSL read error: %d\n", err);
                        ERR_print_errors_fp(stderr);
                    }
                    
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                    continue;
                }

                if(recv_len <= 0)
                {
                    printf("Recieved nothing\n");
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    continue;
                }
                else if(recv_len > (ssize_t)sizeof(request) - 1)
                {
                    printf("Length Exceeded\n");
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    continue;
                }

                request[recv_len] = '\0';
                printf("https Recieved %d\n", recv_len);	
                printf("https Received from client %d: \n%s", i, request);

                struct Client* client = init_request(request, https_client_sockets[i], true);
                if(client == NULL)
                {
                    printf("Failed to initialized client\n");
                    free(client);
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                    continue;
                }
                
                client->client_fd = https_client_sockets[i];
                client->client_ip = inet_ntoa(client_addr.sin_addr);
                client->client_port = ntohs(client_addr.sin_port);
                client->ssl = ssl;

                client->fd = process_request(client, tree_head);
                if(client->fd < 0)
                {
                    printf("Could not process request\n");
                    free(client);
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                    continue;
                }
                else if(client->fd == 1) //Cached Response
                {
                    printf("Cached Response\n");
                    free(client->full_path);
                    free(client);
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                    continue;
                }

                if(strncmp(client->method, "GET", 3) == 0 || strncmp(client->method, "HEAD", 4) == 0 || strncmp(client->method, "OPTIONS", 7) == 0 || strncmp(client->method, "TRACE", 5) == 0)
                {
                    if(send_response(tree_head, client) < 0)
                    {
                        send_code(406, client->client_fd);
                        master_log(406, client);
                        printf("Does not accept filetype(s) html, svg, jpeg\n");
                        free(client->full_path);
                        free(client);
                        SSL_free(ssl);
                        close(https_client_sockets[i]);
                        https_client_sockets[i] = 0;
                        https_client_ssl[i] = NULL;
                        continue;
                    }
                }
                else if(strncmp(client->method, "POST", 4) == 0 || strncmp(client->method, "PUT", 3) == 0 || strncmp(client->method, "DELETE", 6) == 0 || strncmp(client->method, "CONNECT", 7) == 0 || strncmp(client->method, "PATCH", 5) == 0)
                {
                    send_code(501, client->client_fd);
                    master_log(501, client);
                    printf("Methods not Implemented.\n");
                    free(client->full_path);
                    free(client);
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                    continue;
                }
                else
                {
                    send_code(501, client->client_fd);
                    master_log(501, client);
                    printf("Methods not Implemented\n");
                    free(client->full_path);
                    free(client);
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                    continue;
                }

                if(!client->connection_status)
                {
                    printf("https Closed\n");
                    SSL_free(ssl);
                    close(https_client_sockets[i]);
                    https_client_sockets[i] = 0;
                    https_client_ssl[i] = NULL;
                }

                free(client->full_path);
                free(client);
            }
        }
    }

    free_tree(tree_head);
    close(http_sock);
    close(https_sock);

    return 0;
}

struct Client* init_request(char* request, int client_fd, bool is_ssl)
{
    char* cpy_request = strdup(request);

    struct Client* client = malloc(sizeof(struct Client));
    if(!client)
    {
        printf("Unable to allocate space for client.\n");
        send_code(500, client_fd);
        return NULL;
    }

    memset(client, 0, sizeof(struct Client));

    char* tokptr;
    char *line = NULL;   

    line = strtok_r(request, "\r\n", &tokptr);

    if(line == NULL)
    {
        printf("Empty Request\n");
        send_code(400, client_fd);
        master_log(400, client);
        free(client);
        return NULL;
    }

    char* headptr;
    char* str = strtok_r(line, " ", &headptr);
    client->method = str;
    if(client->method == NULL)
    {
        printf("Parse Error: Method\n");
        send_code(400, client_fd);
        master_log(400, client);
        free(cpy_request);
        free(client);
        return NULL;
    }
    else if(strncmp(client->method, "TRACE", 5) == 0)
    {
        printf("Requested TRACE\n");
        client->request = cpy_request;
    }
    else
    {
        free(cpy_request);
    }

    str = strtok_r(NULL, " ", &headptr);
    client->path = str;
    if(client->path == NULL)
    {
        printf("Parse Error: Path\n");
        send_code(400, client_fd);
        master_log(400, client);
        free(client);
        return NULL;
    }

    str = strtok_r(NULL, "\r\n", &headptr);
    client->version = str;
    if(client->version == NULL)
    {
        printf("Parse Error: Version\n");
        send_code(400, client_fd);
        master_log(400, client);
        free(client);
        return NULL;
    }

    if(strlen(client->version) < 5 && strlen(client->version) > 8)
    {
        printf("Error in version\n");
        send_code(400, client_fd);
        master_log(400, client);
        free(client);
        return NULL;
    }

    //keep-alive is implied with HTTP/1.1 but not HTTP/1.0
    if(strncmp(client->version, "HTTP/1.0", 8) == 0)
        client->connection_status = 0;
    else if(strncmp(client->version, "HTTP/1.1", 8) == 0)
        client->connection_status = 1;
    else
    {
        printf("Version Error\n");
        send_code(505, client_fd);
        master_log(505, client);
        free(client);
        return NULL;
    }

    //set to 0 - IF NO TAG THIS VALUE WILL BE USED
    client->tag = 0;

    if(is_ssl)
    {
        client->is_ssl = 1;
    }

    while((line = strtok_r(NULL, "\r\n", &tokptr)) != NULL)
    {
        if((strncmp(line, "Host: ", 6)) == 0)
        {
            line += 6;
            client->host = line;
        }
        else if((strncmp(line, "Connection: ", 12)) == 0)
        {
            line += 12;

            if((strncmp(line, "keep-alive", 10)) == 0)
                client->connection_status = 1;
            else
                client->connection_status = 0;
        }
        else if((strncmp(line, "User-Agent: ", 12)) == 0)
        {
            line += 12;
            client->user_agent = line;
        }
        else if((strncmp(line, "If-None-Match: ", 15)) == 0)
        {
            line += 16;
            line[strlen(line)-1] = '\0';
            client->tag = strtol(line, NULL, 10);
        }
        else if((strncmp(line, "DNT: ", 5)) == 0)
        {
            line += 5;

            if((strncmp(line, "1", 1)) == 0)
                client->DNT = 1;
            else
                client->DNT = 0;
        }
        else if((strncmp(line, "Sec-GPC: ", 8)) == 0)
        {
            line += 8;

            if((strncmp(line, "1", 1)) == 0)
                client->GPC = 1;
            else
                client->GPC = 0;
        }
        else if((strncmp(line, "Upgrade-Insecure-Requests: ", 27)) == 0)
        {
            line += 27;

            if((strncmp(line, "1", 1)) == 0)
                client->upgrade_tls = 1;
            else
                client->upgrade_tls = 0;
        }
        else if((strncmp(line, "Referer: ", 9)) == 0)
        {
            line += 9;

            client->referer = line;
        }
        else if((strncmp(line, "Accept: ", 8)) == 0)
        {
            line += 8;

            client->accept = line;
        }
        else if((strncmp(line, "Accept-Encoding: ", 17)) == 0)
        {
            line += 17;

            client->encoding = line;
        }
        else if((strncmp(line, "Accept-Language: ", 17)) == 0)
        {
            line += 17;

            client->language = line;
        }
        else if((strncmp(line, "Priority: ", 10)) == 0)
        {
            line += 10;

            client->priority = line;
        }
    }


    printf("Printing Client:\n");
    printf("%s %s %s\n", client->method, client->path, client->version);
    printf("Host: %s\n", client->host);
    printf("Keep Alive: %d\n", client->connection_status);
    printf("Tag: %u\n", client->tag);
    printf("DNT: %d\n", client->DNT);
    printf("GPC: %d\n", client->GPC);
    printf("Upgrade-Insecure_Requests: %d\n", client->upgrade_tls);
    printf("User Agent: %s\n", client->user_agent);
    printf("Referer: %s\n", client->referer);
    printf("Accept: %s\n", client->accept);
    printf("Encoding: %s\n", client->encoding);
    printf("Langauge: %s\n", client->language);
    printf("Priority: %s\n", client->priority);
    printf("Is SSL: %d\n", client->is_ssl);
    printf("\n\n");
    
    return client;
}

/*
Returns:
    -1  - Error
    0   - Other Method
    >2  - fd to file 
*/
int process_request(struct Client* client, struct Node* tree_head)
{
    if(!client->method)
    {
        printf("Bad Request\n");
        send_code(500, client->client_fd);
        master_log(500, client);
        return -1;
    }
    
    if(strncmp(client->method, "OPTIONS", 7) == 0 || strncmp(client->method, "TRACE", 5) == 0)
    {
        printf("Control Methods\n");
        return 0;
    }
    if(strncmp(client->method, "GET", 3) != 0 && strncmp(client->method, "HEAD", 4) != 0)
    {
        printf("Not Implemented\n");
        send_code(501, client->client_fd);
        master_log(501, client);
        return 0;
    }

    if(!client->path)
    {
        printf("Bad Request\n");
        send_code(400, client->client_fd);
        master_log(400, client);
        return -1;
    }

    
    char* requested_page = NULL;

    if(strcmp(client->path, "/") == 0)
    {
        requested_page = malloc(sizeof("/landing.html"));
        if(requested_page)
            strcpy(requested_page, "/landing.html");
    }
    else
    {
        requested_page = malloc(strlen(client->path) + 1);
        if(requested_page)
            strcpy(requested_page, client->path);
    }

    printf("rootpath: %s\n", requested_page);

    char path[MAXLINE] = "/home/remote/server/webpages";
    strcat(path, requested_page);
    client->full_path = strdup(path);

    printf("User wants: %s\n", path);

    //403for back a page
    if(strstr(path, "..") != NULL)
    {
        printf("Asked for restricted page (..)\n");
        send_code(403, client->client_fd);
        master_log(403, client);
        free(client->full_path);
        free(requested_page);
        return -1;
    }

    //Check Forbidden (www)
    if(strcmp(path, "/home/remote/server/webpages/www") == 0)
    {
        printf("Asked for restricted page\n");
        send_code(403, client->client_fd);
        master_log(403, client);
        free(client->full_path);
        free(requested_page);
        return -1;
    }

    //check hash
    if(client->tag != 0)
    {
        struct Node* node = lookupNode(tree_head, hashPath(client->full_path));
        printf("File_hash: %u : client->tag: %u \n", node->file_hash, client->tag);

        if(node->file_hash == client->tag)
        {
            char headers[MAX_RESPONSE];
            int header_len = 0;
            memset(headers, 0, sizeof(headers));

            char* date = get_time(0);
            char* week_date = get_time(7 * 24 * 60 * 60);

            header_len += sprintf(headers + header_len, "%s 304 Not Modified\r\n", client->version);
            header_len += sprintf(headers + header_len, "Accept-Ranges: bytes\r\n");
            header_len += sprintf(headers + header_len, "ETag: \"%u\"\r\n", client->tag);
            header_len += sprintf(headers + header_len, "Date: %s\r\n", date);
            header_len += sprintf(headers + header_len, "Expires: %s\r\n", week_date);
            header_len += sprintf(headers + header_len, "Last-Modified: %s\r\n", node->last_modified); //changed to per resource
            header_len += sprintf(headers + header_len, "Server: %s\r\n", SERVER);
            header_len += sprintf(headers + header_len, "Connection: close\r\n\r\n");

            if(client->is_ssl)
            { 
                if(SSL_write(client->ssl, headers, header_len) < 0)
                {
                    printf("Error sending https response.\n");
                }
            }
            else
            {
                if(send(client->client_fd, headers, header_len, 0) < 0)
                {
                    printf("Error sending http response.\n");
                }
            }

            printf("Response:\n%s\n", headers);
            master_log(304, client);

            free(date);
            free(week_date);
            free(requested_page);
            return 1;
        }
    }

    //Check April Fools
    if(strcmp(path, "/home/remote/server/webpages/teapot") == 0)
    {
        printf("April fools joke\n");
        send_code(418, client->client_fd);
        master_log(418, client);
        free(client->full_path);
        free(requested_page);
        return -1;
    }

    int fd = open(path, O_RDONLY);
    if(fd < 0)
    {
        printf("Not opened\n");
        send_code(404, client->client_fd);
        master_log(404, client);
        free(client->full_path);
        free(requested_page);
        return -1;
    }

    printf("Path: %s\n", path);
    printf("Requested-Page: %s\n", requested_page);

    free(requested_page);
    return fd;
}

/*
-1 - Error
0 - Different Method
1 - Cached
>2 - fd
*/
int send_response(struct Node* head, struct Client* client) 
{
    char headers[MAX_RESPONSE];
    int header_len = 0;
    memset(headers, 0, sizeof(headers));

    char* date = get_time(0);

    if(strncmp(client->method, "OPTIONS", 7) == 0)
    {
        header_len += sprintf(headers + header_len, "%s 204 No Content\r\n", client->version);
        header_len += sprintf(headers + header_len, "Allow: GET, HEAD, OPTIONS, TRACE\r\n");
        header_len += sprintf(headers + header_len, "Date: %s\r\n", date);
        header_len += sprintf(headers + header_len, "Server: %s\r\n", SERVER);
        header_len += sprintf(headers + header_len, "Connection: keep-alive\r\n\r\n");

        if(client->is_ssl)
        {
            if(SSL_write(client->ssl, headers, header_len) < 0)
            {
                printf("Error sending https response.\n");
            }
        }
        else
        {
            if(send(client->client_fd, headers, header_len, 0) < 0)
            {
                printf("Error sending http response.\n");
            }
        }

        printf("Response:\n%s\n", headers);
        master_log(200, client);

        free(date);
        return 0;
    }

    if(strncmp(client->method, "TRACE", 5) == 0)
    {
        header_len += sprintf(headers + header_len, "%s 200 OK\r\n", client->version);
        header_len += sprintf(headers + header_len, "Content-Length: %d\r\n", strlen(client->request));
        header_len += sprintf(headers + header_len, "Date: %s\r\n", date);
        header_len += sprintf(headers + header_len, "Server: %s\r\n", SERVER);
        header_len += sprintf(headers + header_len, "Content-Type: message/http\r\n\r\n");

        header_len += sprintf(headers + header_len, client->request);

        if(client->is_ssl)
        {
            if(SSL_write(client->ssl, headers, header_len) < 0)
            {
                printf("Error sending https response.\n");
            }
        }
        else
        {
            if(send(client->client_fd, headers, header_len, 0) < 0)
            {
                printf("Error sending http response.\n");
            }
        }

        printf("Response:\n%s\n", headers);
        master_log(200, client);

        free(date);
        return 0;
    }  

    char* week_date = get_time(7 * 24 * 60 * 60);
    
    char* file_type = content_type(client->full_path);
    if(!file_type) 
    {
        printf("File_type not supported\n");
        send_code(406, client->client_fd);
        master_log(406, client);
        free(date);
        free(week_date);
        return -1;   
    }

    struct Node* node = lookupNode(head, hashPath(client->full_path)); 
    
    header_len += sprintf(headers + header_len, "%s 200 OK\r\n", client->version);
    header_len += sprintf(headers + header_len, "Accept-Ranges: bytes\r\n");
    header_len += sprintf(headers + header_len, "Content-Type: %s\r\n", file_type);
    header_len += sprintf(headers + header_len, "ETag: \"%u\"\r\n", node->file_hash);
    header_len += sprintf(headers + header_len, "Date: %s\r\n", date);
    header_len += sprintf(headers + header_len, "Expires: %s\r\n", week_date);
    header_len += sprintf(headers + header_len, "Last-Modified: %s\r\n", node->last_modified);
    header_len += sprintf(headers + header_len, "Server: %s\r\n", SERVER);
    if(client->connection_status)
        header_len += sprintf(headers + header_len, "Connection: keep-alive\r\n");
    
    printf("Hashingpath: %u\n", node->file_hash);

    //send headers seperately from data
    if(strncmp(client->method, "GET", 3) == 0)   
    {
        off_t file_size = lseek(client->fd, 0, SEEK_END);
        lseek(client->fd, 0, SEEK_SET);
        
        header_len += sprintf(headers + header_len, "Content-Length: %ld\r\n\r\n", file_size);
        
        int n;
        if(client->is_ssl)
            n = SSL_write(client->ssl, headers, header_len);
        else
            n = send(client->client_fd, headers, header_len, 0);

            

        if(n < 0) 
        {
            perror("Send headers failed");
            close(client->fd);
            free(file_type);
            free(week_date);
            free(date);
            return -1;
        }
        printf("Sent: %d\n%s\n", header_len, headers);

        char file_buffer[MAX_RESPONSE];

        while (1) 
        {
            int n = read(client->fd, file_buffer, sizeof(file_buffer));
            if(n < 0)
            {
                printf("Error in read");
                return -1;
            }
            else if(n == 0)
            {
                printf("EOF\n");
                break;
            }
            else if(client->is_ssl)
            {
                if (SSL_write(client->ssl, file_buffer, n) < 0) 
                {
                    printf("Send file failed");
                    break;
                } 
            }
            else
            {
                if (send(client->client_fd, file_buffer, n, 0) < 0) 
                {
                    printf("Send file failed");
                    break;
                }  
            } 
            printf("Sent: %d\n%s\n", n, file_buffer);
            memset(file_buffer, 0, sizeof(file_buffer));
        }
    }
    else 
    {
        header_len += sprintf(headers + header_len, "\r\n");

        if(client->is_ssl)
            SSL_write(client->ssl, headers, header_len);
        else
            send(client->client_fd, headers, header_len, 0);
    }

    master_log(200, client);
    
    close(client->fd);
    free(file_type);
    free(week_date);
    free(date);
    return 0;
}

//<3 abv day>, <day #> <month> <year> <time: ##:##:##> GMT
char* get_time(int offset)
{
    time_t now = time(NULL);
    now += offset;
    struct tm tm = *gmtime(&now);

    char* str = malloc(MAXLINE);
    strftime(str, MAXLINE, "%a, %d %b %Y %H:%M:%S GMT", &tm);

    return str;
}


char* content_type(char* filepath)
{
    printf("Path: %s\n", filepath);

    char* type = malloc(MAXLINE);
    if(strstr(filepath, ".html") != NULL)
    {
        strcpy(type, "text/html");
    }
    else if(strstr(filepath, ".ico") != NULL)
    {
        strcpy(type, "image/svg+xml");
    }
    else if(strstr(filepath, ".jpg") != NULL)
    {
        strcpy(type, "image/jpg");
    }
    else if(strstr(filepath, ".jpeg") != NULL)
    {
        strcpy(type, "image/jpeg");
    }
    else if(strstr(filepath, ".c") != NULL)
    {
        strcpy(type, "text/x-c");
    }
    else if(strstr(filepath, ".h") != NULL)
    {
        strcpy(type, "text/x-h");
    }
    else
        return NULL;

    return type;
}

int master_log(int code, struct Client* client)
{
    int fd = open("/home/remote/server/debug.txt", O_CREAT | O_APPEND | O_WRONLY, 0774);
    if(fd < 0)
    {
        printf("fd open error in log\n");
        return -1;
    }

    char* date = get_time(0);
    char* response = malloc(MAXLINE);

    sprintf(response, "%s, %s:%d, %d, %s\n", date, client->client_ip, client->client_port, code, client->full_path);

    if(write(fd, response, strlen(response)) < 0)
    {
        printf("Write error in log\n");
        return -1;
    }

    free(response);
    free(date);
    close(fd);

    return 1;
}

/*
    1  -
    0  -
    -1 -
*/
int send_code(int code, int client_fd)
{
    int fd;
    char buffer[MAXLINE];
    off_t offset = 0;

    char* teapot = "I'm a teapot, I cannot brew coffee.";
    
    char* path = malloc(MAXLINE);
    strcpy(path,"/home/remote/server/webpages/error_pages");
    char* response = malloc(MAX_RESPONSE);
    char* date = get_time(0);   

    switch(code)
    {
        case 200:
            printf("OK\n");
            break;
        case 201:
            printf("Created\n");
            break;
        case 301:
            printf("Redirected to a different place\n");
            break;
        case 304:
            
            break;
        case 308:
            printf("Resource Permenatly Changed\n");
            break;
        case 400:
            strcat(path, "/400.html");
            fd = open(path, O_RDONLY);
            offset = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
            
            sprintf(response, "HTTP/1.1 400 Bad Request\r\nDate: %s\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", date, offset);
            while(1)
            {
                int n = read(fd, buffer, sizeof(buffer));
                if(n < 0)
                {
                    printf("Read Error in 400");
                    return -1;
                }
                else if(n == 0)
                {
                    printf("EOF\n");
                    break;
                }
                else
                {
                    strncat(response, buffer, n);
                }
            }
            
            if(send(client_fd, response, strlen(response), 0) < 0)
                printf("Send Failed\n");

            printf("Response: %s\n", response);
            break;
        case 403:
            strcat(path, "/403.html");
            fd = open(path, O_RDONLY);
            offset = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
            
            sprintf(response, "HTTP/1.1 403 Forbidden\r\nDate: %s\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", date, offset);
            while(1)
            {
                int n = read(fd, buffer, sizeof(buffer));
                if(n < 0)
                {
                    printf("Read Error in 403\n");
                    return -1;
                }
                else if(n == 0)
                {
                    printf("EOF\n");
                    break;
                }
                else
                {
                    strncat(response, buffer, n);
                }
            }
            
            if(send(client_fd, response, strlen(response), 0) < 0)
                printf("Send Failed\n");

            break;
        case 404:
            strcat(path, "/404.html");
            fd = open(path, O_RDONLY);
            offset = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
            
            sprintf(response, "HTTP/1.1 404 Not Found\r\nDate: %s\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", date, offset);
            while(1)
            {
                int n = read(fd, buffer, sizeof(buffer));
                if(n < 0)
                {
                    printf("Read Error in 404");
                    return -1;
                }
                else if(n == 0)
                {
                    printf("EOF\n");
                    break;
                }
                else
                {
                    strncat(response, buffer, n);
                }
            }
            
            if(send(client_fd, response, strlen(response), 0) < 0)
                printf("Send Failed\n");

            break;
        case 406:

            sprintf(response, "HTTP/1.1 406 Not Acceptable\r\nDate: %s\r\nServer: Snap/0.1\r\nConnection: close\r\n\r\n", date);

            if(send(client_fd, response, strlen(response), 0) < 0)
                printf("Send Failed\n");

            break;
        case 418:
            sprintf(response, "HTTP/1.1 418 I'm a teapot\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", strlen(teapot), teapot);

            if(send(client_fd, response, strlen(response), 0) < 0)
                printf("418 Send Failed\n");
                
            break;
        case 500: 
            printf("Internal Server Error\n");
            break;
        case 501: 
            printf("Not Implemented\n");
            sprintf(response, "HTTP/1.1 501 Not Implemented\r\nConnection: close\r\n\r\n");

            if(send(client_fd, response, strlen(response), 0) < 0)
                printf("501 Send Failed\n");

            break;
        case 505: 
            printf("Version Not Supported\n");
            break;
        default:
            printf("I did not understand\n");
            break;
    }
    printf("Response: %s\n", response);

    free(date);
    free(response);
    free(path);

    return 1;
}
