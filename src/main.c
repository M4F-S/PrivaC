#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "server.h"
#include "vault.h"

static vault_manager_t g_vault_mgr;

static void handle_sigint(int sig) {
    (void)sig;
    printf("\nShutting down Aegis Proxy and wiping session vaults...\n");
    vault_manager_destroy(&g_vault_mgr);
    exit(0);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --port <port>          Port to listen on (default: 8080)\n");
    printf("  --upstream <host:port> Upstream LLM endpoint (e.g. 127.0.0.1:11434)\n");
    printf("  --mock                 Enable built-in mock LLM streaming engine (default if no upstream)\n");
    printf("  --no-pii               Disable PII / name entity masking\n");
    printf("  --no-num               Disable numeric variable extraction\n");
    printf("  --no-inst              Disable automatic symbolic math directive injection\n");
    printf("  --verbose, -v          Enable verbose request / token logging\n");
    printf("  --help, -h             Show this help message\n");
}

int main(int argc, char **argv) {
    server_config_t config;
    memset(&config, 0, sizeof(config));
    config.port = 8080;
    config.upstream_host = NULL;
    config.upstream_port = 0;
    config.mock_mode = true;
    config.verbose = false;
    config.mask_cfg = mask_config_default();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--upstream") == 0 && i + 1 < argc) {
            char *up = argv[++i];
            char *colon = strchr(up, ':');
            if (colon) {
                *colon = '\0';
                config.upstream_host = up;
                config.upstream_port = atoi(colon + 1);
                config.mock_mode = false;
            } else {
                config.upstream_host = up;
                config.upstream_port = 80;
                config.mock_mode = false;
            }
        } else if (strcmp(argv[i], "--mock") == 0) {
            config.mock_mode = true;
        } else if (strcmp(argv[i], "--no-pii") == 0) {
            config.mask_cfg.pii_masking_enabled = false;
        } else if (strcmp(argv[i], "--no-num") == 0) {
            config.mask_cfg.numeric_masking_enabled = false;
        } else if (strcmp(argv[i], "--no-inst") == 0) {
            config.mask_cfg.inject_symbolic_instructions = false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            config.verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    vault_manager_init(&g_vault_mgr);
    config.vault_mgr = &g_vault_mgr;

    return server_start(&config);
}
