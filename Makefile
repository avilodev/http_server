CC = gcc

# SERVER_PATH: absolute path to the project root, injected into every .c file
# as the -DSERVER_PATH="..." compiler flag.  Override at build time with:
#   make SERVER_PATH=/custom/path
SERVER_PATH ?= $(abspath .)

INC_DIR = include

CFLAGS  = -Wall -Wextra -pthread -O2 -g -I$(INC_DIR) -DSERVER_PATH=\"$(SERVER_PATH)\"
LDFLAGS = -lssl -lcrypto -lsqlite3 -lsodium

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Source subdirectories — VPATH tells make where to search for %.c prerequisites
VPATH = $(SRC_DIR):$(SRC_DIR)/http:$(SRC_DIR)/api:$(SRC_DIR)/net:$(SRC_DIR)/cache:$(SRC_DIR)/core

# Source files (basenames only — VPATH resolves actual paths at build time)
SOURCES = main.c \
          request.c response.c error_pages.c \
          api.c post.c \
          ssl_handler.c thread_pool.c \
          cache.c node.c hash_table.c mime.c \
          logger.c config.c utils.c session.c

OBJECTS = $(addprefix $(OBJ_DIR)/, $(SOURCES:.c=.o))
TARGET  = $(BIN_DIR)/server
HEADERS = $(wildcard $(INC_DIR)/*.h)

.PHONY: all clean rebuild run debug directories

all: directories $(TARGET)

# Create necessary runtime and build directories
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) var/log var/db

# Link
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Compile — VPATH resolves %.c to the correct subdirectory
# Any header change triggers a full rebuild
$(OBJ_DIR)/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

rebuild: clean all

run: $(TARGET)
	sudo ./$(TARGET)

debug:
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"
	@echo "Target:  $(TARGET)"
	@echo "VPATH:   $(VPATH)"
