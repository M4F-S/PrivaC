#include "server.h"
#include "sse_parser.h"
#include "../deps/yyjson/yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>

#define BUFFER_SIZE 65536

typedef struct {
    int client_fd;
    server_config_t config;
} client_thread_arg_t;

/* Helper to send all bytes */
static bool send_all(int fd, const char *data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, data + total, len - total, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                continue;
            }
            return false;
        }
        total += (size_t)n;
    }
    return true;
}

/* Serve mock streaming LLM response for demonstration and offline testing */
static void handle_mock_stream(int client_fd, session_vault_t *session, arena_t *arena, bool verbose) {
    const char *http_header = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n";
    send_all(client_fd, http_header, strlen(http_header));

    sse_stream_handler_t handler;
    sse_stream_handler_init(&handler, session, arena);

    /* Construct realistic fragmented LLM chunks containing entity and calc tags */
    const char *raw_chunks[] = {
        "data: {\"choices\":[{\"delta\":{\"content\":\"Based on the \"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"financial figures for \"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"Entity_A, \"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"the net profit calculation \"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"is derived as: <<ca\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"lc: (VAR_1 - \"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"VAR_2) * (1 - VAR_3)>>\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\" after applying all taxes.\"}}]}\n\n",
        "data: [DONE]\n\n"
    };

    size_t num_chunks = sizeof(raw_chunks) / sizeof(raw_chunks[0]);
    for (size_t i = 0; i < num_chunks; i++) {
        char out_buf[4096];
        size_t written = sse_stream_process_data(&handler, raw_chunks[i], strlen(raw_chunks[i]), out_buf, sizeof(out_buf));
        if (written > 0) {
            if (verbose) {
                printf("[Proxy SSE Out] %s", out_buf);
                fflush(stdout);
            }
            send_all(client_fd, out_buf, written);
        }
        usleep(20000); /* 20 ms delay to simulate realistic streaming */
    }
}

/* Forward request to upstream HTTP server and stream response back to client */
static void forward_upstream(int client_fd, 
                             const server_config_t *config, 
                             const char *masked_body, 
                             size_t body_len, 
                             session_vault_t *session, 
                             arena_t *arena) {
    struct hostent *he = gethostbyname(config->upstream_host ? config->upstream_host : "127.0.0.1");
    if (!he) {
        const char *err = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 20\r\n\r\nUpstream DNS Failed\n";
        send_all(client_fd, err, strlen(err));
        return;
    }

    int upstream_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (upstream_fd < 0) {
        const char *err = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 21\r\n\r\nSocket creation error\n";
        send_all(client_fd, err, strlen(err));
        return;
    }

    struct sockaddr_in up_addr;
    memset(&up_addr, 0, sizeof(up_addr));
    up_addr.sin_family = AF_INET;
    up_addr.sin_port = htons((uint16_t)(config->upstream_port > 0 ? config->upstream_port : 11434));
    memcpy(&up_addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    if (connect(upstream_fd, (struct sockaddr *)&up_addr, sizeof(up_addr)) < 0) {
        close(upstream_fd);
        const char *err = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 25\r\n\r\nCannot connect upstream\n";
        send_all(client_fd, err, strlen(err));
        return;
    }

    /* Send HTTP POST header & masked body */
    char req_header[1024];
    int req_header_len = snprintf(req_header, sizeof(req_header),
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        config->upstream_host, config->upstream_port, body_len);
    send_all(upstream_fd, req_header, (size_t)req_header_len);
    send_all(upstream_fd, masked_body, body_len);

    /* Read upstream response headers and stream body */
    sse_stream_handler_t handler;
    sse_stream_handler_init(&handler, session, arena);

    char in_buf[BUFFER_SIZE];
    ssize_t n;
    bool headers_passed = false;

    while ((n = recv(upstream_fd, in_buf, sizeof(in_buf) - 1, 0)) > 0) {
        in_buf[n] = '\0';

        if (!headers_passed) {
            char *body_start = strstr(in_buf, "\r\n\r\n");
            if (body_start) {
                headers_passed = true;
                size_t header_bytes = (size_t)(body_start + 4 - in_buf);
                send_all(client_fd, in_buf, header_bytes);

                const char *body_part = body_start + 4;
                size_t body_part_len = (size_t)(n - header_bytes);
                if (body_part_len > 0) {
                    char out_buf[BUFFER_SIZE];
                    size_t written = sse_stream_process_data(&handler, body_part, body_part_len, out_buf, sizeof(out_buf));
                    if (written > 0) send_all(client_fd, out_buf, written);
                }
            } else {
                send_all(client_fd, in_buf, (size_t)n);
            }
        } else {
            char out_buf[BUFFER_SIZE];
            size_t written = sse_stream_process_data(&handler, in_buf, (size_t)n, out_buf, sizeof(out_buf));
            if (written > 0) {
                send_all(client_fd, out_buf, written);
            }
        }
    }

    close(upstream_fd);
}

static void *client_thread_worker(void *arg) {
    client_thread_arg_t *carg = (client_thread_arg_t *)arg;
    int client_fd = carg->client_fd;
    server_config_t config = carg->config;
    free(carg);

    arena_t req_arena;
    arena_init(&req_arena, 64 * 1024);

    char *buf = (char *)malloc(BUFFER_SIZE);
    if (!buf) {
        close(client_fd);
        arena_destroy(&req_arena);
        return NULL;
    }

    ssize_t bytes_read = recv(client_fd, buf, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        free(buf);
        close(client_fd);
        arena_destroy(&req_arena);
        return NULL;
    }
    buf[bytes_read] = '\0';

    /* Parse Request Line */
    if (strncmp(buf, "GET /health", 11) == 0) {
        const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n\r\n{\"status\":\"ok\"}";
        send_all(client_fd, resp, strlen(resp));
    } else if (strncmp(buf, "POST /v1/chat/completions", 25) == 0) {
        /* Extract session ID header if provided, otherwise default */
        char session_id[64] = "default-session";
        char *sess_hdr = strstr(buf, "x-session-id: ");
        if (sess_hdr) {
            sess_hdr += 14;
            char *sess_end = strstr(sess_hdr, "\r\n");
            if (sess_end) {
                size_t slen = (size_t)(sess_end - sess_hdr);
                if (slen < sizeof(session_id)) {
                    strncpy(session_id, sess_hdr, slen);
                    session_id[slen] = '\0';
                }
            }
        }

        session_vault_t *session = vault_get_or_create_session(config.vault_mgr, session_id);

        /* Find JSON body */
        char *body = strstr(buf, "\r\n\r\n");
        if (body) {
            body += 4;
            size_t body_len = (size_t)(bytes_read - (body - buf));

            /* Mask JSON request payload */
            char *masked_json = mask_engine_process_json_request(session, &req_arena, body, body_len, &config.mask_cfg);
            if (!masked_json) {
                masked_json = body;
            }

            if (config.verbose) {
                printf("[Proxy] Session: %s\n", session_id);
                printf("[Proxy Masked Request]: %s\n", masked_json);
            }

            if (config.mock_mode || !config.upstream_host) {
                handle_mock_stream(client_fd, session, &req_arena, config.verbose);
            } else {
                forward_upstream(client_fd, &config, masked_json, strlen(masked_json), session, &req_arena);
            }
        } else {
            const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nMissing body";
            send_all(client_fd, err, strlen(err));
        }
    } else {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
        send_all(client_fd, not_found, strlen(not_found));
    }

    free(buf);
    close(client_fd);
    arena_destroy(&req_arena);
    return NULL;
}

int server_start(const server_config_t *config) {
    if (!config) return -1;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)config->port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("==================================================================\n");
    printf("  AEGIS BLIND C PROXY & SYMBOLIC ENGINE (Port %d)\n", config->port);
    printf("  Mode: %s\n", config->mock_mode ? "Mock LLM Engine (Offline Demo)" : "Upstream Forwarder");
    if (!config->mock_mode && config->upstream_host) {
        printf("  Upstream: %s:%d\n", config->upstream_host, config->upstream_port);
    }
    printf("  OpenAI Endpoint: http://127.0.0.1:%d/v1/chat/completions\n", config->port);
    printf("==================================================================\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        client_thread_arg_t *arg = (client_thread_arg_t *)malloc(sizeof(client_thread_arg_t));
        if (arg) {
            arg->client_fd = client_fd;
            arg->config = *config;
            pthread_t tid;
            pthread_create(&tid, NULL, client_thread_worker, arg);
            pthread_detach(tid);
        } else {
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
