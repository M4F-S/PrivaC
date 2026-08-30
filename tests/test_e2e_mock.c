#include "../include/sse_parser.h"
#include "../include/mask_engine.h"
#include "../include/vault.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("Running test_e2e_mock...\n");

    vault_manager_t vm;
    vault_manager_init(&vm);
    session_vault_t *session = vault_get_or_create_session(&vm, "e2e_session_42");

    arena_t arena;
    arena_init(&arena, 64 * 1024);

    mask_config_t cfg = mask_config_default();

    /* Step 1: Inbound client prompt */
    const char *client_req = 
        "{\"model\":\"gpt-4\",\"messages\":[{\"role\":\"user\",\"content\":\"Alice made $1,250,000 in revenue with $820,000 in expenses. What is her net profit after a 22% tax?\"}],\"stream\":true}";

    char *masked_req = mask_engine_process_json_request(session, &arena, client_req, strlen(client_req), &cfg);
    assert(masked_req != NULL);
    assert(strstr(masked_req, "Entity_A") != NULL);
    assert(strstr(masked_req, "VAR_1") != NULL);
    assert(strstr(masked_req, "VAR_2") != NULL);
    assert(strstr(masked_req, "VAR_3") != NULL);

    /* Step 2: Outbound Mock LLM SSE stream */
    sse_stream_handler_t sse_handler;
    sse_stream_handler_init(&sse_handler, session, &arena);

    const char *mock_sse_chunks[] = {
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Entity_A's net profit is \"}}]}\n\n",
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"<<ca\"}}]}\n\n",
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"lc: (VAR_1 - VAR_2)\"}}]}\n\n",
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\" * (1 - VAR_3)>>\"}}]}\n\n",
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\" after taxes.\"}}]}\n\n",
        "data: [DONE]\n\n"
    };

    char full_client_stream[4096] = "";
    for (size_t i = 0; i < sizeof(mock_sse_chunks)/sizeof(mock_sse_chunks[0]); i++) {
        char out_buf[1024];
        size_t n = sse_stream_process_data(&sse_handler, mock_sse_chunks[i], strlen(mock_sse_chunks[i]), out_buf, sizeof(out_buf));
        if (n > 0) {
            strcat(full_client_stream, out_buf);
        }
    }

    printf("Full Decoded SSE Stream Output:\n%s\n", full_client_stream);

    /* Assert that client received Alice, the computed $335,400.00, and standard SSE framing */
    assert(strstr(full_client_stream, "Alice's net profit is") != NULL);
    assert(strstr(full_client_stream, "$335,400.00") != NULL);
    assert(strstr(full_client_stream, "data: [DONE]") != NULL);

    arena_destroy(&arena);
    vault_manager_destroy(&vm);
    printf("✓ test_e2e_mock passed successfully!\n");
    return 0;
}
