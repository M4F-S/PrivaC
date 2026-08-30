#ifndef PRIVAC_MASK_ENGINE_H
#define PRIVAC_MASK_ENGINE_H

#include <stddef.h>
#include <stdbool.h>
#include "vault.h"
#include "arena.h"

typedef struct {
    bool pii_masking_enabled;
    bool numeric_masking_enabled;
    bool inject_symbolic_instructions;
    const char *custom_instruction;
} mask_config_t;

/* Get default masking configuration */
mask_config_t mask_config_default(void);

/* Mask a raw prompt string: extracts entities and numbers into vault, 
   rewrites prompt with surrogates and VAR_N, and optionally injects symbolic directives */
char *mask_engine_process_text(session_vault_t *session, 
                               arena_t *arena, 
                               const char *input_text, 
                               const mask_config_t *config);

/* Process an entire OpenAI/Anthropic JSON request payload: 
   selectively masks messages[i].content and returns modified JSON string */
char *mask_engine_process_json_request(session_vault_t *session, 
                                       arena_t *arena, 
                                       const char *json_payload, 
                                       size_t json_len, 
                                       const mask_config_t *config);

/* Unmask a full text response (replaces Entity_A -> Alice, etc.) */
char *mask_engine_unmask_text(const session_vault_t *session, 
                              arena_t *arena, 
                              const char *masked_text);

#endif /* PRIVAC_MASK_ENGINE_H */
