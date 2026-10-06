#define _XOPEN_SOURCE 700
#include "types.h"
#include "request.h"
#include "response.h"
#include "thread_pool.h"
#include "logger.h"
#include "ssl_handler.h"
#include "cache.h"
#include "config.h"
#include "mime.h"
#include "api.h"
#include "post.h"
#include "session.h"
#include "error_pages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h> 

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include <openssl/err.h>

#include <sqlite3.h>
#include <sodium.h>

// Global variables for signal handling
static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_refresh_cache = 0;

// Global thread pool
static struct thread_pool* g_thread_pool = NULL;

//Initialize Mime Hash Table
ht* mime_table;

//sql database
sqlite3* g_database = NULL;
pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global cache tree and its rwlock
static struct node_t* g_cache_tree = NULL;
static pthread_rwlock_t g_cache_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Server start time for uptime calculation
time_t g_server_start = 0;

void* handle_client_thread(void* arg);

/**
 * Signal handler for managing server and cache operations.
 * 
 * Handles system signals to control server shutdown and cache refresh.
 * This function is async-signal-safe and only modifies sig_atomic_t variables.
 * 
 * @param signum The signal number that triggered this handler
 * 
 * @note SIGINT/SIGTERM/SIGQUIT trigger graceful shutdown
 * @note SIGUSR1 triggers cache tree refresh
 * @warning This function runs in signal context - keep it minimal
 */
void signal_handler(int signum) {
	// async-signal-safe: only write() and volatile sig_atomic_t assignments
	switch (signum) {
	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		write(STDERR_FILENO, "\nShutdown signal received\n", 26);
		g_shutdown = 1;
		break;
	case SIGUSR1:
		write(STDERR_FILENO, "Cache refresh signal received\n", 30);
		g_refresh_cache = 1;
		break;
	default:
		break;
	}
}

/**
 * Setup signal handlers for server management.
 * 
 * Configures handlers for shutdown signals (SIGINT/SIGTERM/SIGQUIT),
 * cache refresh signal (SIGUSR1), and ignores SIGPIPE.
 * 
 * @note SIGPIPE is ignored to prevent crashes on broken connections
 * @note All other signals invoke signal_handler()
 */
void setup_signals(void) {
	signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
}

/**
 * Main worker thread for every new client
 *
 * Read and logs client request, checks for HTTP upgrades, 
 *  validates asking path, checks for cached responses (304), 
 *  then sends file. All resources are cleaned up before thread exit.
 *
 * @param arg ThreadArgs* object - holds client info
 *
 * @return Always returns NULL (pthread requirement)
 *
 * @note This function is called by thread pool workers
 * @note All allocated resources (Client, SSL, ThreadArgs) are freed internally
 * @warning Do not call directly - submit via threadpool_add_work()
 *
 * @see threadpool_add_work(), parse_http_request(), send_file_response()
 */
// Compares two HTTP-date strings
static int compare_http_dates(const char* a, const char* b) {
	struct tm ta, tb;

	memset(&ta, 0, sizeof(ta));
	memset(&tb, 0, sizeof(tb));

	if(!strptime(a, "%a, %d %b %Y %H:%M:%S %Z", &ta))
		return 0;
	if(!strptime(b, "%a, %d %b %Y %H:%M:%S %Z", &tb))
		return 0;

	if(ta.tm_year != tb.tm_year)
		return ta.tm_year - tb.tm_year;
	if(ta.tm_mon  != tb.tm_mon)
		return ta.tm_mon  - tb.tm_mon;
	if(ta.tm_mday != tb.tm_mday)
		return ta.tm_mday - tb.tm_mday;
	if(ta.tm_hour != tb.tm_hour)
		return ta.tm_hour - tb.tm_hour;
	if(ta.tm_min  != tb.tm_min)
		return ta.tm_min  - tb.tm_min;

	return ta.tm_sec - tb.tm_sec;
}

// Serves a file: cache check (304), open, send.
// The cache lock is held only for the lookup, never during network I/O.
// Sets client->connection_status = 0 if the connection should be closed.
static void serve_static(client_t* client) {
	struct node_t  snap;
	struct node_t* cache_node = NULL;
	char* last_mod_copy = NULL;

	// Copy what we need out of the cache, then release the lock right away
	pthread_rwlock_rdlock(&g_cache_rwlock);
	struct node_t* found = cache_lookup(g_cache_tree, client->full_path);
	if(found) {
		memset(&snap, 0, sizeof(snap));
		snap.file_hash = found->file_hash;
		if(found->last_modified) {
			last_mod_copy = strdup(found->last_modified);
			snap.last_modified = last_mod_copy;
		}
		cache_node = &snap;
	}
	pthread_rwlock_unlock(&g_cache_rwlock);

	int not_modified = 0;

	if(cache_node && cache_node->last_modified && client->modified_since &&
	   compare_http_dates(cache_node->last_modified, client->modified_since) <= 0) {
		log_message(LOG_INFO, "Resource not modified (If-Modified-Since) - sending 304");
		not_modified = 1;
	} else if(cache_node && client->tag != 0 && cache_node->file_hash == client->tag) {
		log_message(LOG_INFO, "ETag match (client: %u, cache: %u) - sending 304",
					client->tag, cache_node->file_hash);
		not_modified = 1;
	}

	if(not_modified) {
		send_not_modified_response(client, cache_node);
	} else {
		int ok = 1;

		// HEAD requests only need metadata, so skip open()
		if(strcmp(client->method, "HEAD") != 0) {
			client->fd = open(client->full_path, O_RDONLY);
			if(client->fd < 0) {
				if(errno == ENOENT) {
					log_message(LOG_WARN, "File not found: %s", client->full_path);
					send_error_response(404, client);
				} else if(errno == EACCES) {
					log_message(LOG_WARN, "Permission denied: %s", client->full_path);
					send_error_response(403, client);
				} else {
					log_message(LOG_ERROR, "Failed to open file %s: %s",
								client->full_path, strerror(errno));
					send_error_response(500, client);
				}
				client->connection_status = 0;
				ok = 0;
			}
		}

		if(ok && send_file_response(client, cache_node) < 0) {
			log_message(LOG_ERROR, "Failed to send file response");
			client->connection_status = 0;
		}
	}

	free(last_mod_copy);
}

// Accepts one connection from a listening socket and hands it to the thread pool.
// Pass ssl_ctx = NULL for plain HTTP. The TLS handshake happens later, in the worker.
static void accept_and_queue(int listen_sock, SSL_CTX* ssl_ctx) {
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);

	int client_fd = accept(listen_sock, (struct sockaddr*)&ss, &len);
	if(client_fd < 0) {
		if(errno == EMFILE || errno == ENFILE) {
			log_message(LOG_ERROR, "accept(): out of file descriptors");
			struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000 };
			nanosleep(&delay, NULL);
		} else if(errno != EINTR) {
			log_message(LOG_ERROR, "accept() failed: %s", strerror(errno));
		}
		return;
	}

	char ip[INET6_ADDRSTRLEN];
	int  port;
	int  ipv6 = (ss.ss_family == AF_INET6);

	if(ipv6) {
		struct sockaddr_in6* a = (struct sockaddr_in6*)&ss;
		inet_ntop(AF_INET6, &a->sin6_addr, ip, sizeof(ip));
		port = ntohs(a->sin6_port);
	} else {
		struct sockaddr_in* a = (struct sockaddr_in*)&ss;
		inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip));
		port = ntohs(a->sin_port);
	}

	SSL* ssl = NULL;
	if(ssl_ctx) {
		ssl = SSL_new(ssl_ctx);
		if(!ssl) {
			close(client_fd);
			return;
		}
		SSL_set_fd(ssl, client_fd);   // no handshake here
	}

	thread_args* args = malloc(sizeof(thread_args));
	if(!args) {
		if(ssl)
			SSL_free(ssl);
		close(client_fd);
		return;
	}

	args->client_fd   = client_fd;
	args->ssl         = ssl;
	args->client_port = port;
	snprintf(args->client_ip, sizeof(args->client_ip), "%s", ip);

	if(ipv6)
		log_message(LOG_INFO, "New %s connection from [%s]:%d", ssl ? "HTTPS" : "HTTP", ip, port);
	else
		log_message(LOG_INFO, "New %s connection from %s:%d", ssl ? "HTTPS" : "HTTP", ip, port);

	if(threadpool_add_work(g_thread_pool, handle_client_thread, args) != 0) {
		log_message(LOG_WARN, "Thread pool queue full, rejecting connection");
		if(ssl)
			SSL_free(ssl);
		close(client_fd);
		free(args);
	}
}

void* handle_client_thread(void* arg) {
	thread_args* args = (thread_args*)arg;
	extern struct server_config g_config;
	int requests_served = 0;

	// Timeouts first, so nothing below can block forever
	struct timeval tv = { .tv_sec = HANDSHAKE_TIMEOUT_SEC, .tv_usec = 0 };
	setsockopt(args->client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	tv.tv_sec = SEND_TIMEOUT_SEC;
	setsockopt(args->client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	// TLS handshake now runs here in the worker, not in the accept loop
	if(args->ssl) {
		if(SSL_accept(args->ssl) <= 0) {
			ERR_clear_error();
			SSL_free(args->ssl);
			args->ssl = NULL;       // so cleanup doesn't touch it again
			goto cleanup;
		}
	}

	// Idle timeout between requests
	tv.tv_sec = KEEPALIVE_TIMEOUT_SEC;
	setsockopt(args->client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	while(1) {
		char request_buffer[8192];
		memset(request_buffer, 0, sizeof(request_buffer));

		ssize_t recv_len;
		if(args->ssl)
			recv_len = SSL_read(args->ssl, request_buffer, sizeof(request_buffer) - 1);
		else
			recv_len = recv(args->client_fd, request_buffer, sizeof(request_buffer) - 1, 0);

		if(recv_len <= 0) {
			if(recv_len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				log_message(LOG_INFO, "Keep-alive idle timeout, closing connection");
			else
				log_message(LOG_WARN, "Client disconnected or read error");
			ERR_clear_error();
			goto cleanup;
		}

		request_buffer[recv_len] = '\0';
		log_message(LOG_DEBUG, "Received %ld bytes from client", recv_len);

		client_t* client = parse_http_request(request_buffer, args->client_fd, args->ssl);
		if(!client) {
			log_message(LOG_ERROR, "Failed to parse request");
			goto cleanup;
		}

		client->client_ip   = strdup(args->client_ip);
		client->client_port = args->client_port;

		// Cap requests per connection: the response will say "Connection: close"
		if(++requests_served >= MAX_REQUESTS_PER_CONN)
			client->connection_status = 0;

		log_message(LOG_INFO, "Request from %s:%d - %s %s %s",
					client->client_ip, client->client_port,
					client->method, client->path, client->version);

		print_client_info(client);

		// Handle TLS upgrade redirect (HTTP only)
		if(!args->ssl && client->upgrade_tls) {
			char redirect_url[512];
			snprintf(redirect_url, sizeof(redirect_url), "https://%s%s",
					 client->host ? client->host : "localhost", client->path);
			log_message(LOG_INFO, "Redirecting to HTTPS: %s", redirect_url);
			send_redirect_response(redirect_url, client);
			free_client(client);
			goto cleanup;
		}

		// Validate HTTP method
		if(!validate_http_method(client->method)) {
			if(strcmp(client->method, "OPTIONS") == 0) {
				log_message(LOG_INFO, "Handling OPTIONS request");
				send_options_response(client);
			} else {
				log_message(LOG_WARN, "Unsupported method: %s", client->method);
				send_error_response(501, client);
			}
			free_client(client);
			goto cleanup;
		}

		if(strncmp(client->method, "POST", 4) == 0) {
			handle_post(client);
			free_client(client);
			goto cleanup;
		}

		// Validate path for security
		if(!validate_path(client->path)) {
			log_message(LOG_WARN, "Invalid/dangerous path detected: %s", client->path);
			send_error_response(403, client);
			free_client(client);
			goto cleanup;
		}

		// Resolve full filesystem path
		client->full_path = resolve_request_path(client->path, g_config.webroot);
		if(!client->full_path) {
			log_message(LOG_ERROR, "Failed to resolve path");
			send_error_response(500, client);
			free_client(client);
			goto cleanup;
		}

		log_message(LOG_INFO, "Resolved path: %s", client->full_path);

		// API endpoint
		if(strncmp(client->path, "/api/", 5) == 0) {
			log_message(LOG_INFO, "API endpoint detected - %s", client->full_path);
			handle_api_request(client);
			free_client(client);
			goto cleanup;
		}

		// Session enforcement for protected pages
		{
			static const char* protected_prefixes[] = {
				"/landing", "/dashboard", "/profile", NULL
			};
			int is_protected = 0;

			for(int i = 0; protected_prefixes[i]; i++) {
				if(strncmp(client->path, protected_prefixes[i],
						   strlen(protected_prefixes[i])) == 0) {
					is_protected = 1;
					break;
				}
			}

			if(is_protected && !session_is_valid(client->session_token)) {
				log_message(LOG_INFO, "Unauthenticated access to %s - redirecting to login",
							client->path);
				send_redirect_response("/login.html", client);
				free_client(client);
				goto cleanup;
			}
		}

		serve_static(client);

		int keep_alive = client->connection_status;
		free_client(client);
		if(!keep_alive)
			goto cleanup;
	}

cleanup:
	if(args->ssl) {
		SSL_shutdown(args->ssl);
		SSL_free(args->ssl);
	}

	close(args->client_fd);
	free(args);

	return NULL;
}

/**
 * Sets up and opens a new socket on a specified port
 *
 * Creates a socket, and specifies the option to rebind to the port if still open.
 *  This socket binds and listens on a specified port for IPv4 connections only.
 *
 * @param port Port for which OS will bind and listen for connections on
 *
 * @return File Descriptor for the new socket opened
 *
 * @note This function is called by both the HTTP and HTTPS sockets
 * @warning Caller must close() the returned socket when done
 */
int create_server_socket(int port) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if(sock < 0) {
		perror("socket creation failed");
		return -1;
	}

	// Set SO_REUSEADDR to allow quick restart
	int opt = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror("setsockopt SO_REUSEADDR failed");
		close(sock);
		return -1;
	}

	// Bind to address
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	if(bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind failed");
		close(sock);
		return -1;
	}

	// Listen for connections
	if(listen(sock, BACKLOG) < 0) {
		perror("listen failed");
		close(sock);
		return -1;
	}

	printf("Server listening on port %d (IPv4)\n", port);

	return sock;
}

/**
 * Creates an IPv6-only TCP server socket on the given port.
 * IPv4 clients use the separate IPv4 socket; this handles IPv6-native clients.
 *
 * @param port Port to bind
 * @return Socket fd, or -1 on failure (non-fatal: IPv6 may be unavailable)
 */
int create_server_socket6(int port) {
	int sock = socket(AF_INET6, SOCK_STREAM, 0);

	if(sock < 0)
		return -1;

	int opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// IPV6_V6ONLY=1: accept IPv6 connections only; IPv4 handled by the other socket
	int v6only = 1;
	setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

	struct sockaddr_in6 addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_addr   = in6addr_any;
	addr.sin6_port   = htons(port);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(sock);
		return -1;
	}

	if(listen(sock, BACKLOG) < 0) {
		close(sock);
		return -1;
	}

	printf("Server listening on port %d (IPv6)\n", port);

	return sock;
}

/**
 * Main control flow for the program
 * 
 * Sets up log files, ssl, HTTP and HTTPS sockets and threads. 
 *  Waits for new connections on both a HTTP and HTTPS socket. Invokes
 *  Handle Client Thread to handle client and closes. After main loop, 
 *  cleanup logs, ssl, and HTTP/HTTPS sockets.
 * 
 * @param argc Counts how many argument were passed in when executed
 * @param argv Stores the arguments passed in on execution
 *
 * @return 0 on successful shutdown, 1 on initialization error
 *
 * @note All clients are handled by handle_client_thread
 * @warning Must cleanup ThreadArgs object in handle_worker_thread
 * @see handle_client_thread(), setup_signals(), threadpool_create()
 */
int main(int argc, char** argv) {
	printf("=== HTTP/HTTPS Server Starting ===\n");
	printf("Server: Snap/0.4\n");
	printf("PID: %d\n", getpid());

	// Load configuration
	if(load_config(argc, argv) < 0) {
		fprintf(stderr, "Usage: %s -v <video_directory>\n", argv[0]);
		return 1;
	}

	extern server_config g_config;

	// Record server start time
	g_server_start = time(NULL);

	// Initialize logger
	log_init(SERVER_PATH "/var/log/server.log");
	log_message(LOG_INFO, "Server starting - PID: %d", getpid());

	// Setup signal handlers
	setup_signals();

	// Initialize libsodium (for password hashing)
	printf("Initializing libsodium...\n");

	if(sodium_init() < 0) {
		fprintf(stderr, "Failed to initialize libsodium\n");
		log_message(LOG_ERROR, "Failed to initialize libsodium");
		log_close();
		return 1;
	}

	log_message(LOG_INFO, "libsodium initialized successfully");

	// Initialize database
	printf("Initializing database...\n");

	if(init_database(&g_database) != SQLITE_OK) {
		fprintf(stderr, "Failed to initialize database\n");
		log_message(LOG_ERROR, "Failed to initialize database");
		log_close();
		return 1;
	}

	log_message(LOG_INFO, "Database initialized successfully");

	// Initialize cache tree
	g_cache_tree = cache_tree_init(g_config.webroot);
	if(!g_cache_tree) {
		log_message(LOG_ERROR, "Failed to initialize cache tree");
		return 1;
	}

	// Load error pages into memory
	error_pages_init(g_config.webroot);

	// Initialize OpenSSL
	init_openssl();
	SSL_CTX* ssl_ctx = create_ssl_context();
	if(!ssl_ctx) {
		log_message(LOG_ERROR, "Failed to create SSL context");
		cache_tree_free(g_cache_tree);
		cleanup_openssl();
		return 1;
	}

	configure_ssl_context(ssl_ctx);

	// Create IPv4 HTTP/HTTPS sockets
	int http_sock = create_server_socket(g_config.http_port);
	if(http_sock < 0) {
		log_message(LOG_ERROR, "Failed to create HTTP socket");
		SSL_CTX_free(ssl_ctx);
		cleanup_openssl();
		cache_tree_free(g_cache_tree);
		return 1;
	}

	int https_sock = create_server_socket(g_config.https_port);
	if(https_sock < 0) {
		log_message(LOG_ERROR, "Failed to create HTTPS socket");
		close(http_sock);
		SSL_CTX_free(ssl_ctx);
		cleanup_openssl();
		cache_tree_free(g_cache_tree);
		return 1;
	}

	// Create IPv6 sockets (optional — non-fatal if IPv6 is unavailable)
	int http6_sock  = create_server_socket6(g_config.http_port);
	int https6_sock = create_server_socket6(g_config.https_port);
	if(http6_sock  < 0)
		log_message(LOG_WARN, "IPv6 HTTP socket unavailable");
	if(https6_sock < 0)
		log_message(LOG_WARN, "IPv6 HTTPS socket unavailable");

	// Create thread pool
	struct thread_pool_config pool_config = {
		.num_threads = g_config.thread_pool_size,
		.max_queue_size = g_config.max_queue_size
	};

	g_thread_pool = threadpool_create(pool_config);
	if(!g_thread_pool) {
		log_message(LOG_ERROR, "Failed to create thread pool");
		close(http_sock);
		close(https_sock);
		SSL_CTX_free(ssl_ctx);
		cleanup_openssl();
		cache_tree_free(g_cache_tree);
		return 1;
	}

	char mime_table_path[256];
	snprintf(mime_table_path, sizeof(mime_table_path), "%s/etc/mime.types", SERVER_PATH);

	mime_table = mime_init(mime_table_path);
	if(mime_table == NULL) {
		fprintf(stderr, "Failed to load MIME types\n");
		return 1;
	}

	log_message(LOG_INFO, "Server initialized successfully");
	printf("=== Server Ready ===\n");
	printf("HTTP Port: %d\n", g_config.http_port);
	printf("HTTPS Port: %d\n", g_config.https_port);
	printf("Thread Pool Size: %d\n", g_config.thread_pool_size);
	printf("Press Ctrl+C to shutdown\n");
	printf("Send SIGUSR1 (kill -USR1 %d) to refresh cache\n", getpid());

	time_t last_session_cleanup = time(NULL);
	// Main server loop
	while(!g_shutdown) {
		if(g_shutdown)
			break;

		// Handle cache refresh signal
		if(g_refresh_cache) {
			log_message(LOG_INFO, "Refreshing cache tree");

			// Acquire write lock — blocks until all readers (worker threads) finish
			pthread_rwlock_wrlock(&g_cache_rwlock);
			cache_tree_refresh(&g_cache_tree, g_config.webroot);
			pthread_rwlock_unlock(&g_cache_rwlock);

			g_refresh_cache = 0;
			log_message(LOG_INFO, "Cache refresh complete");
		}

		// Remove expired sessions every 5 minutes
		time_t now_t = time(NULL);
		if(now_t - last_session_cleanup >= 300) {
			session_cleanup_expired();
			last_session_cleanup = now_t;
		}

		// Setup select() for all active sockets (IPv4 + optional IPv6)
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(http_sock,  &read_fds);
		FD_SET(https_sock, &read_fds);
		int max_fd = (http_sock > https_sock) ? http_sock : https_sock;
		if(http6_sock  >= 0) {
			FD_SET(http6_sock,  &read_fds);
			if(http6_sock  > max_fd)
				max_fd = http6_sock;
		}
		if(https6_sock >= 0) {
			FD_SET(https6_sock, &read_fds);
			if(https6_sock > max_fd)
				max_fd = https6_sock;
		}

		// Use timeout so we can check signals periodically
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

		if(activity < 0) {
			if(errno == EINTR) {
				// Interrupted by signal, continue to check shutdown flag
				continue;
			}
			log_message(LOG_ERROR, "select() failed: %s", strerror(errno));
			break;
		}

		if(activity == 0) {
			// Timeout, no activity - just loop again to check signals
			continue;
		}

		if(FD_ISSET(http_sock, &read_fds))
			accept_and_queue(http_sock, NULL);

		if(FD_ISSET(https_sock, &read_fds))
			accept_and_queue(https_sock, ssl_ctx);

		if(http6_sock >= 0 && FD_ISSET(http6_sock, &read_fds))
			accept_and_queue(http6_sock, NULL);

		if(https6_sock >= 0 && FD_ISSET(https6_sock, &read_fds))
			accept_and_queue(https6_sock, ssl_ctx);
	}

	// Shutdown sequence
	printf("\n=== Shutting down server ===\n");
	log_message(LOG_INFO, "Server shutdown initiated");

	// Stop accepting new connections
	close(http_sock);
	close(https_sock);
	if(http6_sock  >= 0)
		close(http6_sock);
	if(https6_sock >= 0)
		close(https6_sock);
	printf("Closed listening sockets\n");

	// Wait for all pending work to complete
	printf("Waiting for pending requests to complete...\n");
	threadpool_wait(g_thread_pool);

	// Destroy thread pool
	printf("Destroying thread pool...\n");
	threadpool_destroy(g_thread_pool);

	// Cleanup cache tree
	printf("Freeing cache tree...\n");
	cache_tree_free(g_cache_tree);
	pthread_rwlock_destroy(&g_cache_rwlock);

	//Cleanup Mime Table
	printf("Destorying Mime Table\n");
	ht_destroy(mime_table);

	// Close database connection
	if(g_database) {
		printf("Closing database connection...\n");
		sqlite3_close(g_database);
		log_message(LOG_INFO, "Database closed");
	}
	pthread_mutex_destroy(&g_db_mutex);

	// Cleanup session store
	session_store_destroy();

	// Free error pages
	error_pages_free();

	// Cleanup SSL
	printf("Cleaning up SSL...\n");
	SSL_CTX_free(ssl_ctx);
	cleanup_openssl();

	// Cleanup config
	free_config();

	// Close logger
	log_message(LOG_INFO, "Server shutdown complete");
	log_close();

	printf("=== Server stopped ===\n");

	return 0;
}