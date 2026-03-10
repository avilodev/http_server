#ifndef CONFIG_H
#define CONFIG_H

extern struct ServerConfig g_config;

int load_config(int argc, char** argv);
void free_config(void);

#endif