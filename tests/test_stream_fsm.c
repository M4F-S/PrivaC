#include "../include/stream_fsm.h"
#include "../include/vault.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("Running test_stream_fsm...\n");

    vault_manager_t vm;
    vault_manager_init(&vm);
    session_vault_t *session = vault_get_or_create_session(&vm, "stream_test_session");

    session_vault_add_entity(session, "Alice", "Entity");
    session_vault_add_numeric_var(session, "$1,250,000", 1250000.0, NUM_FMT_CURRENCY_USD, 0, true);
    session_vault_add_numeric_var(session, "$820,000", 820000.0, NUM_FMT_CURRENCY_USD, 0, true);
    session_vault_add_numeric_var(session, "22%", 0.22, NUM_FMT_PERCENTAGE, 0, false);

    stream_fsm_t fsm;
    stream_fsm_init(&fsm, session);

    char full_output[2048] = "";
    size_t full_len = 0;

    /* Simulate micro-chunked SSE stream tokens split across boundary */
    const char *chunks[] = {
        "The net profit for ",
        "Entity_A",
        " is calculated as: <<ca",
        "lc: (VAR_1 - ",
        "VAR_2) * (1 - VAR_3)>>",
        " for the current year."
    };

    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        char out_chunk[512];
        size_t n = stream_fsm_process_chunk(&fsm, chunks[i], strlen(chunks[i]), out_chunk, sizeof(out_chunk));
        if (n > 0) {
            strncat(full_output, out_chunk, sizeof(full_output) - full_len - 1);
            full_len += n;
        }
    }

    char flush_buf[256];
    size_t flushed = stream_fsm_flush(&fsm, flush_buf, sizeof(flush_buf));
    if (flushed > 0) {
        strncat(full_output, flush_buf, sizeof(full_output) - full_len - 1);
        full_len += flushed;
    }

    printf("Stream Output: '%s'\n", full_output);

    /* Assert unmasked name and computed formula result */
    assert(strstr(full_output, "The net profit for Alice is calculated as:") != NULL);
    assert(strstr(full_output, "$335,400.00") != NULL);
    assert(strstr(full_output, "for the current year.") != NULL);

    /* Test False-Alarm Prefix Matching (e.g. C++ bitshift operator `<<`) */
    stream_fsm_reset(&fsm);
    char fa_out[512];
    size_t fa_len = stream_fsm_process_chunk(&fsm, "std::cout << val;\n", 18, fa_out, sizeof(fa_out));
    fa_len += stream_fsm_flush(&fsm, fa_out + fa_len, sizeof(fa_out) - fa_len);
    assert(strstr(fa_out, "std::cout << val;") != NULL);

    vault_manager_destroy(&vm);
    printf("✓ test_stream_fsm passed successfully!\n");
    return 0;
}
