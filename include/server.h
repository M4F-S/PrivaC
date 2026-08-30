#ifndef PRIVAC_SERVER_H
#define PRIVAC_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "vault.h"
#include "mask_engine.h"

typedef struct {
    int port;
    const char *upstream_host;
    int upstream_port;
    bool mock_mode;
    bool verbose;
    vault_manager_t *vault_mgr;
    mask_config_t mask_cfg;
} server_config_t;

/* Start reverse proxy server (blocking event loop) */
int server_start(const server_config_t *config);

#endif /* PRIVAC_SERVER_H */
