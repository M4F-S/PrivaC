#include "../include/symbolic_vm.h"
#include "../include/vault.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

int main(void) {
    printf("Running test_math_vm...\n");

    vault_manager_t vm;
    vault_manager_init(&vm);
    session_vault_t *session = vault_get_or_create_session(&vm, "math_session");

    /* Register Variables:
       VAR_1 = 1250000.0 (Revenue, Currency USD)
       VAR_2 = 820000.0  (Expenses, Currency USD)
       VAR_3 = 0.22      (Tax rate, Percentage)
    */
    session_vault_add_numeric_var(session, "$1,250,000", 1250000.0, NUM_FMT_CURRENCY_USD, 0, true);
    session_vault_add_numeric_var(session, "$820,000", 820000.0, NUM_FMT_CURRENCY_USD, 0, true);
    session_vault_add_numeric_var(session, "22%", 0.22, NUM_FMT_PERCENTAGE, 0, false);

    /* Test 1: Net Profit Formula (VAR_1 - VAR_2) * (1 - VAR_3)
       Expected: (1250000 - 820000) * (1 - 0.22) = 430000 * 0.78 = 335400.00 -> "$335,400.00"
    */
    math_vm_result_t r1 = symbolic_vm_eval(session, "(VAR_1 - VAR_2) * (1 - VAR_3)");
    assert(r1.success);
    assert(fabs(r1.raw_result - 335400.0) < 0.001);
    assert(strcmp(r1.formatted_result, "$335,400.00") == 0);

    /* Test 2: Addition and Multiplication */
    math_vm_result_t r2 = symbolic_vm_eval(session, "VAR_1 + VAR_2");
    assert(r2.success);
    assert(fabs(r2.raw_result - 2070000.0) < 0.001);
    assert(strcmp(r2.formatted_result, "$2,070,000.00") == 0);

    /* Test 3: Division by zero guard */
    math_vm_result_t r3 = symbolic_vm_eval(session, "VAR_1 / 0");
    assert(!r3.success);
    assert(strcmp(r3.formatted_result, "[Math Error]") == 0);

    /* Test 4: Invalid syntax guard */
    math_vm_result_t r4 = symbolic_vm_eval(session, "VAR_1 + * VAR_2");
    assert(!r4.success);

    vault_manager_destroy(&vm);
    printf("✓ test_math_vm passed successfully!\n");
    return 0;
}
