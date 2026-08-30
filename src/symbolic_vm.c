#include "symbolic_vm.h"
#include "../deps/tinyexpr/tinyexpr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void symbolic_vm_format_number(double val, number_format_t fmt, int decimals, bool has_commas, char *out, size_t out_len) {
    if (!out || out_len == 0) return;

    if (isnan(val)) {
        snprintf(out, out_len, "[NaN]");
        return;
    }
    if (isinf(val)) {
        snprintf(out, out_len, "[Infinity]");
        return;
    }

    char num_buf[64];
    if (decimals < 0) decimals = 2;

    /* Format raw number into buffer */
    if (decimals == 0) {
        snprintf(num_buf, sizeof(num_buf), "%.0f", val);
    } else {
        snprintf(num_buf, sizeof(num_buf), "%.*f", decimals, val);
    }

    if (!has_commas && fmt != NUM_FMT_CURRENCY_USD) {
        if (fmt == NUM_FMT_PERCENTAGE) {
            snprintf(out, out_len, "%s%%", num_buf);
        } else {
            snprintf(out, out_len, "%s", num_buf);
        }
        return;
    }

    /* Add commas for thousands separators */
    char int_part[64];
    char frac_part[64] = "";
    char *dot = strchr(num_buf, '.');
    if (dot) {
        size_t int_len = (size_t)(dot - num_buf);
        strncpy(int_part, num_buf, int_len);
        int_part[int_len] = '\0';
        strncpy(frac_part, dot, sizeof(frac_part) - 1);
    } else {
        strncpy(int_part, num_buf, sizeof(int_part) - 1);
    }

    char formatted_int[64];
    int in_len = (int)strlen(int_part);
    int is_neg = (in_len > 0 && int_part[0] == '-') ? 1 : 0;
    int out_idx = 0;

    if (is_neg) {
        formatted_int[out_idx++] = '-';
    }

    for (int i = is_neg; i < in_len; i++) {
        formatted_int[out_idx++] = int_part[i];
        int remaining = in_len - i - 1;
        if (remaining > 0 && (remaining % 3 == 0)) {
            formatted_int[out_idx++] = ',';
        }
    }
    formatted_int[out_idx] = '\0';

    if (fmt == NUM_FMT_CURRENCY_USD) {
        if (is_neg) {
            /* -$1,234.56 or -$1,234 */
            snprintf(out, out_len, "-$%s%s", formatted_int + 1, frac_part);
        } else {
            snprintf(out, out_len, "$%s%s", formatted_int, frac_part);
        }
    } else if (fmt == NUM_FMT_PERCENTAGE) {
        snprintf(out, out_len, "%s%s%%", formatted_int, frac_part);
    } else {
        snprintf(out, out_len, "%s%s", formatted_int, frac_part);
    }
}

math_vm_result_t symbolic_vm_eval(const session_vault_t *session, const char *expr_str) {
    math_vm_result_t res;
    memset(&res, 0, sizeof(res));

    if (!expr_str || strlen(expr_str) == 0) {
        res.success = false;
        snprintf(res.error_msg, sizeof(res.error_msg), "Empty expression");
        return res;
    }

    /* Bind variables from session vault */
    te_variable te_vars[PRIVAC_MAX_ENTRIES_PER_SESSION];
    int var_count = 0;
    number_format_t dominant_fmt = NUM_FMT_RAW_FLOAT;
    int max_decimals = 0;
    bool any_commas = false;

    if (session) {
        for (size_t i = 0; i < session->count; i++) {
            if (session->entries[i].type == ENTRY_TYPE_NUMERIC_VAR) {
                te_vars[var_count].name = session->entries[i].surrogate_str;
                te_vars[var_count].address = &session->entries[i].original_numeric;
                te_vars[var_count].type = TE_VARIABLE;
                te_vars[var_count].context = NULL;
                var_count++;

                if (session->entries[i].num_format == NUM_FMT_CURRENCY_USD) {
                    dominant_fmt = NUM_FMT_CURRENCY_USD;
                }
                if (session->entries[i].decimals > max_decimals) {
                    max_decimals = session->entries[i].decimals;
                }
                if (session->entries[i].has_commas) {
                    any_commas = true;
                }
            }
        }
    }

    int err = 0;
    te_expr *expr = te_compile(expr_str, te_vars, var_count, &err);
    if (!expr) {
        res.success = false;
        snprintf(res.error_msg, sizeof(res.error_msg), "Syntax error at col %d", err);
        return res;
    }

    double eval_result = te_eval(expr);
    te_free(expr);

    if (isnan(eval_result) || isinf(eval_result)) {
        res.success = false;
        res.raw_result = eval_result;
        snprintf(res.error_msg, sizeof(res.error_msg), "Division by zero or invalid domain");
        snprintf(res.formatted_result, sizeof(res.formatted_result), "[Math Error]");
        return res;
    }

    res.success = true;
    res.raw_result = eval_result;

    /* If result has no fractional part and we're not currency, display without .00 */
    if (dominant_fmt == NUM_FMT_CURRENCY_USD) {
        if (max_decimals == 0 && floor(eval_result) == eval_result) {
            symbolic_vm_format_number(eval_result, dominant_fmt, 2, true, res.formatted_result, sizeof(res.formatted_result));
        } else {
            symbolic_vm_format_number(eval_result, dominant_fmt, max_decimals > 0 ? max_decimals : 2, true, res.formatted_result, sizeof(res.formatted_result));
        }
    } else {
        if (floor(eval_result) == eval_result) {
            symbolic_vm_format_number(eval_result, NUM_FMT_RAW_INT, 0, any_commas, res.formatted_result, sizeof(res.formatted_result));
        } else {
            symbolic_vm_format_number(eval_result, NUM_FMT_RAW_FLOAT, max_decimals > 0 ? max_decimals : 2, any_commas, res.formatted_result, sizeof(res.formatted_result));
        }
    }

    return res;
}
