#include "../include/mask_engine.h"
#include "../include/vault.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("Running test_mask...\n");

    vault_manager_t vm;
    vault_manager_init(&vm);
    session_vault_t *session = vault_get_or_create_session(&vm, "mask_test_session");

    arena_t arena;
    arena_init(&arena, 32 * 1024);

    mask_config_t cfg = mask_config_default();

    /* Test raw prompt text transformation */
    const char *orig_prompt = "Alice made $1,250,000 in revenue with $820,000 in expenses. What is her net profit after a 22% tax?";
    char *masked_text = mask_engine_process_text(session, &arena, orig_prompt, &cfg);

    assert(masked_text != NULL);
    assert(strstr(masked_text, "Entity_A") != NULL);
    assert(strstr(masked_text, "VAR_1") != NULL);
    assert(strstr(masked_text, "VAR_2") != NULL);
    assert(strstr(masked_text, "VAR_3") != NULL);
    assert(strstr(masked_text, "[SYSTEM DIRECTIVE FOR MATH LOGIC]") != NULL);

    /* Verify unmasking restores original string entities */
    char *unmasked = mask_engine_unmask_text(session, &arena, "The calculated report for Entity_A is complete.");
    assert(strstr(unmasked, "Alice") != NULL);

    /* Test OpenAI JSON request structure transformation */
    const char *json_payload = 
        "{\"model\":\"gpt-4\",\"temperature\":0.7,\"messages\":[{\"role\":\"user\",\"content\":\"Alice made $1,250,000.\"}]}";
    
    char *masked_json = mask_engine_process_json_request(session, &arena, json_payload, strlen(json_payload), &cfg);
    assert(masked_json != NULL);
    assert(strstr(masked_json, "\"model\":\"gpt-4\"") != NULL);
    assert(strstr(masked_json, "\"temperature\":0.7") != NULL);
    assert(strstr(masked_json, "Entity_A") != NULL);
    assert(strstr(masked_json, "VAR_1") != NULL);

    arena_destroy(&arena);
    vault_manager_destroy(&vm);
    printf("✓ test_mask passed successfully!\n");
    return 0;
}
