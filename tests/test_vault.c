#include "../include/vault.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("Running test_vault...\n");

    vault_manager_t vm;
    vault_manager_init(&vm);

    session_vault_t *session = vault_get_or_create_session(&vm, "session_test_1");
    assert(session != NULL);
    assert(strcmp(session->session_id, "session_test_1") == 0);

    /* Test Entity insertion and idempotency */
    const char *e1 = session_vault_add_entity(session, "Alice", "Entity");
    assert(strcmp(e1, "Entity_A") == 0);

    const char *e1_repeat = session_vault_add_entity(session, "Alice", "Entity");
    assert(strcmp(e1_repeat, "Entity_A") == 0);

    const char *e2 = session_vault_add_entity(session, "Bob", "Entity");
    assert(strcmp(e2, "Entity_B") == 0);

    /* Test Numeric Variable insertion */
    const char *v1 = session_vault_add_numeric_var(session, "$1,250,000", 1250000.0, NUM_FMT_CURRENCY_USD, 0, true);
    assert(strcmp(v1, "VAR_1") == 0);

    const char *v2 = session_vault_add_numeric_var(session, "$820,000", 820000.0, NUM_FMT_CURRENCY_USD, 0, true);
    assert(strcmp(v2, "VAR_2") == 0);

    const char *v3 = session_vault_add_numeric_var(session, "22%", 0.22, NUM_FMT_PERCENTAGE, 0, false);
    assert(strcmp(v3, "VAR_3") == 0);

    /* Test Lookups */
    assert(strcmp(session_vault_lookup_original_str(session, "Entity_A"), "Alice") == 0);
    assert(strcmp(session_vault_lookup_original_str(session, "Entity_B"), "Bob") == 0);

    const vault_entry_t *var1_entry = session_vault_lookup_var(session, "VAR_1");
    assert(var1_entry != NULL);
    assert(var1_entry->original_numeric == 1250000.0);
    assert(var1_entry->num_format == NUM_FMT_CURRENCY_USD);

    /* Test Type-Preserving Surrogates */
    int64_t int_surr = session_vault_add_int_surrogate(session, 450000);
    assert(int_surr != 450000);
    assert(int_surr >= 100000);

    double float_surr = session_vault_add_float_surrogate(session, 0.18);
    assert(float_surr != 0.18);

    /* Test Expiry and Cleanup */
    vault_manager_destroy(&vm);
    assert(vm.active_sessions == 0);

    printf("✓ test_vault passed successfully!\n");
    return 0;
}
