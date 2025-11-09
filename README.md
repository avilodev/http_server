## Complete File Organization
```
server/
├── Makefile
├── src/
│   ├── types.h              # Common structures (Client, ThreadArgs, etc.)
│   ├── main.c               # Main loop, accept connections
│   │
│   ├── request.h            # Request parsing interface
│   ├── request.c            # parse_http_request(), validate_path(), etc.
│   │
│   ├── response.h           # Response generation interface
│   ├── response.c           # send_file_response(), send_error_response(), etc.
│   │
│   ├── ssl_handler.h        # SSL/TLS interface
│   ├── ssl_handler.c        # init_openssl(), SSL context creation
│   │
│   ├── cache.h              # Cache interface
│   ├── cache.c              # Cache lookup, tree management
│   │
│   ├── logger.h             # Logging interface
│   ├── logger.c             # log_message(), log_request()
│   │
│   ├── thread_pool.h        # Thread pool interface
│   ├── thread_pool.c        # Worker threads, work queue
│   │
│   ├── config.h             # Configuration interface
│   ├── config.c             # Parse command line, config files
│   │
│   ├── utils.h              # Utility functions
│   ├── utils.c              # get_content_type(), format_date(), etc.
│   │
│   └── node.h               # Node structure for parsing
│       node.c
│
└── obj/                     # Build artifacts


/**
 * Brief one-line description of what the function does.
 * 
 * Longer detailed description if needed. Explain the purpose,
 * behavior, and any important details about the function.
 * Can span multiple lines.
 * 
 * @param param_name Description of the parameter
 * @param another_param Description of another parameter
 * 
 * @return Description of return value (or @retval for specific values)
 * 
 * @note Additional notes or special information
 * @warning Important warnings about usage
 * @see Related functions or references
 */