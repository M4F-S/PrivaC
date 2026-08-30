#ifndef AEGIS_SYMBOLIC_VM_H
#define AEGIS_SYMBOLIC_VM_H

#include <stddef.h>
#include <stdbool.h>
#include "vault.h"

typedef struct {
    bool success;
    double raw_result;
    char formatted_result[128];
    char error_msg[64];
} math_vm_result_t;

/* Evaluate a symbolic expression string (e.g., "(VAR_1 - VAR_2) * (1 - VAR_3)") 
   against the variables stored in the given session vault */
math_vm_result_t symbolic_vm_eval(const session_vault_t *session, const char *expr_str);

/* Helper to format numbers with currency commas, decimals, and symbols */
void symbolic_vm_format_number(double val, number_format_t fmt, int decimals, bool has_commas, char *out, size_t out_len);

#endif /* AEGIS_SYMBOLIC_VM_H */
