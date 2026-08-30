#include "mask_engine.h"
#include "../deps/yyjson/yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *DEFAULT_INSTRUCTION = 
    "\n\n[SYSTEM DIRECTIVE FOR MATH LOGIC]: Derive mathematical calculations symbolically using the defined algebraic variables (e.g., VAR_1, VAR_2). Wrap all formula expressions inside <<calc: expression>> (e.g., <<calc: (VAR_1 - VAR_2) * (1 - VAR_3)>>). Do NOT calculate the numbers yourself; output only the exact formula inside the <<calc: ...>> tag.";

mask_config_t mask_config_default(void) {
    mask_config_t cfg;
    cfg.pii_masking_enabled = true;
    cfg.numeric_masking_enabled = true;
    cfg.inject_symbolic_instructions = true;
    cfg.custom_instruction = NULL;
    return cfg;
}

static bool is_currency_start(const char *p) {
    if (*p == '$') return true;
    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA3) return true; /* £ */
    if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x82 && (unsigned char)p[2] == 0xAC) return true; /* € */
    return false;
}

/* Parse a number token (currency, percentage, or plain float/int) */
static bool parse_number_token(const char *str, size_t *token_len, double *val_out, number_format_t *fmt_out, int *decimals_out, bool *has_commas_out) {
    const char *p = str;
    bool is_curr = false;
    if (is_currency_start(p)) {
        is_curr = true;
        p++;
    }

    if (!isdigit((unsigned char)*p)) {
        return false;
    }

    char clean_num[64];
    size_t c_idx = 0;
    int dec_count = 0;
    int decimals = 0;
    bool comma_seen = false;
    bool in_decimals = false;

    while (*p && (isdigit((unsigned char)*p) || *p == ',' || *p == '.')) {
        if (*p == ',') {
            comma_seen = true;
            p++;
            continue;
        }
        if (*p == '.') {
            if (dec_count > 0) break;
            dec_count++;
            in_decimals = true;
            if (c_idx < sizeof(clean_num) - 1) {
                clean_num[c_idx++] = *p;
            }
            p++;
            continue;
        }

        if (in_decimals) {
            decimals++;
        }
        if (c_idx < sizeof(clean_num) - 1) {
            clean_num[c_idx++] = *p;
        }
        p++;
    }
    clean_num[c_idx] = '\0';

    bool is_pct = false;
    if (*p == '%') {
        is_pct = true;
        p++;
    }

    /* Trailing punctuation like period or comma after number */
    if (c_idx == 0) return false;

    double num = atof(clean_num);
    *token_len = (size_t)(p - str);
    *has_commas_out = comma_seen;
    *decimals_out = decimals;

    if (is_curr) {
        *fmt_out = NUM_FMT_CURRENCY_USD;
        *val_out = num;
    } else if (is_pct) {
        *fmt_out = NUM_FMT_PERCENTAGE;
        /* In math formulas, 22% is 0.22 */
        *val_out = num / 100.0;
    } else {
        *fmt_out = (decimals > 0) ? NUM_FMT_RAW_FLOAT : NUM_FMT_RAW_INT;
        *val_out = num;
    }

    return true;
}

/* Check if word is an email address */
static bool check_email(const char *str, size_t *token_len) {
    const char *p = str;
    const char *at = NULL;
    while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ';' && *p != '"' && *p != '\'') {
        if (*p == '@') at = p;
        p++;
    }
    if (at && at > str && (p - at) > 3) {
        const char *dot = strchr(at, '.');
        if (dot && dot > at + 1 && dot < p - 1) {
            *token_len = (size_t)(p - str);
            return true;
        }
    }
    return false;
}

char *mask_engine_process_text(session_vault_t *session, 
                               arena_t *arena, 
                               const char *input_text, 
                               const mask_config_t *config) {
    if (!input_text) return NULL;
    if (!session || !arena) return (char *)input_text;

    size_t in_len = strlen(input_text);
    /* Allocate output buffer with ample growth margin */
    size_t out_cap = in_len * 3 + 1024;
    char *out = (char *)arena_alloc(arena, out_cap);
    if (!out) return NULL;

    size_t out_pos = 0;
    const char *p = input_text;
    bool has_vars = false;

    while (*p) {
        /* Check email */
        if (config->pii_masking_enabled) {
            size_t email_len = 0;
            if (check_email(p, &email_len)) {
                char email_buf[128];
                size_t cpy_len = email_len < sizeof(email_buf) - 1 ? email_len : sizeof(email_buf) - 1;
                strncpy(email_buf, p, cpy_len);
                email_buf[cpy_len] = '\0';

                const char *surrogate = session_vault_add_entity(session, email_buf, "Email");
                size_t surr_len = strlen(surrogate);
                memcpy(&out[out_pos], surrogate, surr_len);
                out_pos += surr_len;
                p += email_len;
                continue;
            }
        }

        /* Check numbers / currency / percentage */
        if (config->numeric_masking_enabled) {
            size_t num_len = 0;
            double num_val = 0.0;
            number_format_t fmt = NUM_FMT_RAW_INT;
            int decimals = 0;
            bool has_commas = false;

            if (parse_number_token(p, &num_len, &num_val, &fmt, &decimals, &has_commas)) {
                /* Exclude tiny integers like 0 or 1 unless currency or percent */
                char orig_raw[64];
                size_t cpy_len = num_len < sizeof(orig_raw) - 1 ? num_len : sizeof(orig_raw) - 1;
                strncpy(orig_raw, p, cpy_len);
                orig_raw[cpy_len] = '\0';

                const char *var_name = session_vault_add_numeric_var(session, orig_raw, num_val, fmt, decimals, has_commas);
                size_t var_len = strlen(var_name);
                memcpy(&out[out_pos], var_name, var_len);
                out_pos += var_len;
                p += num_len;
                has_vars = true;
                continue;
            }
        }

        /* Check proper names (e.g. Alice, Bob, Charlie) */
        if (config->pii_masking_enabled && isupper((unsigned char)*p)) {
            const char *w_end = p;
            while (*w_end && isalpha((unsigned char)*w_end)) {
                w_end++;
            }
            size_t word_len = (size_t)(w_end - p);
            /* Check known names or heuristic capitalization (length > 2) */
            if (word_len >= 3 && word_len <= 30) {
                char word[64];
                strncpy(word, p, word_len);
                word[word_len] = '\0';

                /* Filter out common English capitalized words at sentence starts */
                static const char *skip_words[] = {
                    "The", "What", "How", "Why", "When", "Where", "Derive", 
                    "Calculate", "Find", "Compute", "Return", "Please", "Given", "And", "With"
                };
                bool is_common = false;
                for (size_t i = 0; i < sizeof(skip_words)/sizeof(skip_words[0]); i++) {
                    if (strcmp(word, skip_words[i]) == 0) {
                        is_common = true;
                        break;
                    }
                }

                if (!is_common) {
                    const char *surrogate = session_vault_add_entity(session, word, "Entity");
                    size_t surr_len = strlen(surrogate);
                    memcpy(&out[out_pos], surrogate, surr_len);
                    out_pos += surr_len;
                    p = w_end;
                    continue;
                }
            }
        }

        out[out_pos++] = *p++;
    }

    /* If variables were detected and symbolic instruction injection is enabled, append instruction */
    if (has_vars && config->inject_symbolic_instructions) {
        const char *inst = config->custom_instruction ? config->custom_instruction : DEFAULT_INSTRUCTION;
        size_t inst_len = strlen(inst);
        if (out_pos + inst_len < out_cap) {
            memcpy(&out[out_pos], inst, inst_len);
            out_pos += inst_len;
        }
    }

    out[out_pos] = '\0';
    return out;
}

char *mask_engine_process_json_request(session_vault_t *session, 
                                       arena_t *arena, 
                                       const char *json_payload, 
                                       size_t json_len, 
                                       const mask_config_t *config) {
    if (!json_payload || json_len == 0) return NULL;

    yyjson_doc *doc = yyjson_read_opts((char *)json_payload, json_len, 0, NULL, NULL);
    if (!doc) {
        return mask_engine_process_text(session, arena, json_payload, config);
    }

    yyjson_mut_doc *mut_doc = yyjson_doc_mut_copy(doc, NULL);
    yyjson_doc_free(doc);
    if (!mut_doc) return NULL;

    yyjson_mut_val *root = yyjson_mut_doc_get_root(mut_doc);
    yyjson_mut_val *messages = yyjson_mut_obj_get(root, "messages");

    if (yyjson_mut_is_arr(messages)) {
        size_t idx, max = yyjson_mut_arr_size(messages);
        yyjson_mut_val *msg;
        yyjson_mut_arr_foreach(messages, idx, max, msg) {
            yyjson_mut_val *content = yyjson_mut_obj_get(msg, "content");
            if (yyjson_mut_is_str(content)) {
                const char *orig_text = yyjson_mut_get_str(content);
                char *masked_text = mask_engine_process_text(session, arena, orig_text, config);
                if (masked_text) {
                    yyjson_mut_set_str(content, masked_text);
                }
            }
        }
    }

    size_t out_len = 0;
    char *out_json = yyjson_mut_write(mut_doc, 0, &out_len);
    yyjson_mut_doc_free(mut_doc);

    if (!out_json) return NULL;

    char *arena_json = arena_strdup(arena, out_json);
    free(out_json);
    return arena_json;
}

char *mask_engine_unmask_text(const session_vault_t *session, 
                              arena_t *arena, 
                              const char *masked_text) {
    if (!session || !masked_text) return (char *)masked_text;

    size_t text_len = strlen(masked_text);
    size_t out_cap = text_len * 3 + 512;
    char *out = (char *)arena_alloc(arena, out_cap);
    if (!out) return NULL;

    size_t out_pos = 0;
    const char *p = masked_text;

    while (*p) {
        bool replaced = false;
        for (size_t i = 0; i < session->count; i++) {
            const char *surr = session->entries[i].surrogate_str;
            size_t surr_len = strlen(surr);
            if (strncmp(p, surr, surr_len) == 0) {
                const char *orig = session->entries[i].original_str;
                size_t orig_len = strlen(orig);
                if (out_pos + orig_len < out_cap) {
                    memcpy(&out[out_pos], orig, orig_len);
                    out_pos += orig_len;
                    p += surr_len;
                    replaced = true;
                    break;
                }
            }
        }

        if (!replaced) {
            out[out_pos++] = *p++;
        }
    }

    out[out_pos] = '\0';
    return out;
}
