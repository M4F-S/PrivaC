#include "stream_fsm.h"
#include "symbolic_vm.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void stream_fsm_init(stream_fsm_t *fsm, const session_vault_t *session) {
    if (!fsm) return;
    fsm->state = FSM_STATE_PASSTHROUGH;
    fsm->buf_len = 0;
    fsm->prefix_len = 0;
    fsm->buffer[0] = '\0';
    fsm->session = session;
}

void stream_fsm_reset(stream_fsm_t *fsm) {
    if (!fsm) return;
    fsm->state = FSM_STATE_PASSTHROUGH;
    fsm->buf_len = 0;
    fsm->prefix_len = 0;
    fsm->buffer[0] = '\0';
}

static inline void append_to_out(char *out_buf, size_t out_cap, size_t *out_pos, char c) {
    if (*out_pos + 1 < out_cap) {
        out_buf[(*out_pos)++] = c;
    }
}

static inline void append_str_to_out(char *out_buf, size_t out_cap, size_t *out_pos, const char *str) {
    size_t len = strlen(str);
    if (*out_pos + len < out_cap) {
        memcpy(&out_buf[*out_pos], str, len);
        *out_pos += len;
    }
}

size_t stream_fsm_flush(stream_fsm_t *fsm, char *out_buf, size_t out_cap) {
    if (!fsm || !out_buf || out_cap == 0) return 0;

    size_t out_pos = 0;
    if (fsm->buf_len > 0) {
        if (fsm->state == FSM_STATE_IN_ENTITY_TAG) {
            /* Check if the buffered entity exists in vault */
            fsm->buffer[fsm->buf_len] = '\0';
            const char *orig = session_vault_lookup_original_str(fsm->session, fsm->buffer);
            append_str_to_out(out_buf, out_cap, &out_pos, orig);
        } else {
            /* Flush raw buffered content */
            fsm->buffer[fsm->buf_len] = '\0';
            append_str_to_out(out_buf, out_cap, &out_pos, fsm->buffer);
        }
        fsm->buf_len = 0;
        fsm->state = FSM_STATE_PASSTHROUGH;
    }
    out_buf[out_pos] = '\0';
    return out_pos;
}

size_t stream_fsm_process_chunk(stream_fsm_t *fsm, 
                                const char *in_chunk, 
                                size_t in_len, 
                                char *out_buf, 
                                size_t out_cap) {
    if (!fsm || !in_chunk || in_len == 0 || !out_buf || out_cap == 0) return 0;

    size_t out_pos = 0;
    const char *p = in_chunk;
    const char *end = in_chunk + in_len;

    while (p < end) {
        char c = *p++;

        switch (fsm->state) {
            case FSM_STATE_PASSTHROUGH:
                if (c == '<') {
                    fsm->state = FSM_STATE_MATCHING_PREFIX;
                    fsm->buffer[0] = '<';
                    fsm->buf_len = 1;
                } else if (c == 'E' || c == 'V') {
                    /* Potential start of Entity_ or VAR_ */
                    fsm->state = FSM_STATE_IN_ENTITY_TAG;
                    fsm->buffer[0] = c;
                    fsm->buf_len = 1;
                } else {
                    append_to_out(out_buf, out_cap, &out_pos, c);
                }
                break;

            case FSM_STATE_MATCHING_PREFIX: {
                /* Looking for "<<calc:" or "<<calc : " */
                static const char *CALC_PREFIX = "<<calc:";
                static const size_t CALC_PREFIX_LEN = 7;

                if (fsm->buf_len < sizeof(fsm->buffer) - 1) {
                    fsm->buffer[fsm->buf_len++] = c;
                    fsm->buffer[fsm->buf_len] = '\0';
                }

                if (fsm->buf_len < CALC_PREFIX_LEN) {
                    if (strncmp(fsm->buffer, CALC_PREFIX, fsm->buf_len) != 0) {
                        /* False alarm! E.g. "<< " or "<foo>" -> flush buffered text */
                        append_str_to_out(out_buf, out_cap, &out_pos, fsm->buffer);
                        fsm->buf_len = 0;
                        fsm->state = FSM_STATE_PASSTHROUGH;
                    }
                } else if (fsm->buf_len == CALC_PREFIX_LEN) {
                    if (strcmp(fsm->buffer, CALC_PREFIX) == 0) {
                        /* Successfully matched <<calc: -> transition to accumulating expr */
                        fsm->state = FSM_STATE_IN_CALC_EXPR;
                        fsm->buf_len = 0; /* Reset buffer to only hold expr */
                    } else {
                        /* False alarm */
                        append_str_to_out(out_buf, out_cap, &out_pos, fsm->buffer);
                        fsm->buf_len = 0;
                        fsm->state = FSM_STATE_PASSTHROUGH;
                    }
                }
                break;
            }

            case FSM_STATE_IN_CALC_EXPR:
                if (c == '>' && fsm->buf_len > 0 && fsm->buffer[fsm->buf_len - 1] == '>') {
                    /* Detected closing >> */
                    fsm->buffer[fsm->buf_len - 1] = '\0'; /* Remove first > */

                    /* Trim whitespace from expression */
                    char *expr_str = fsm->buffer;
                    while (*expr_str && isspace((unsigned char)*expr_str)) expr_str++;
                    char *expr_end = expr_str + strlen(expr_str) - 1;
                    while (expr_end > expr_str && isspace((unsigned char)*expr_end)) {
                        *expr_end = '\0';
                        expr_end--;
                    }

                    /* Evaluate formula in Micro-Math VM */
                    math_vm_result_t res = symbolic_vm_eval(fsm->session, expr_str);
                    if (res.success) {
                        append_str_to_out(out_buf, out_cap, &out_pos, res.formatted_result);
                    } else {
                        /* On error, output formatted error tag */
                        append_str_to_out(out_buf, out_cap, &out_pos, res.formatted_result[0] ? res.formatted_result : "[Math Error]");
                    }

                    fsm->buf_len = 0;
                    fsm->state = FSM_STATE_PASSTHROUGH;
                } else {
                    if (fsm->buf_len < sizeof(fsm->buffer) - 1) {
                        fsm->buffer[fsm->buf_len++] = c;
                    } else {
                        /* Buffer overflow guard: flush as raw text to prevent hang */
                        fsm->buffer[fsm->buf_len] = '\0';
                        append_str_to_out(out_buf, out_cap, &out_pos, "<<calc:");
                        append_str_to_out(out_buf, out_cap, &out_pos, fsm->buffer);
                        fsm->buf_len = 0;
                        fsm->state = FSM_STATE_PASSTHROUGH;
                    }
                }
                break;

            case FSM_STATE_IN_ENTITY_TAG:
                if (isalnum((unsigned char)c) || c == '_') {
                    if (fsm->buf_len < sizeof(fsm->buffer) - 1) {
                        fsm->buffer[fsm->buf_len++] = c;
                    }
                } else {
                    /* End of surrogate token */
                    fsm->buffer[fsm->buf_len] = '\0';
                    const char *orig = session_vault_lookup_original_str(fsm->session, fsm->buffer);
                    append_str_to_out(out_buf, out_cap, &out_pos, orig);
                    fsm->buf_len = 0;
                    fsm->state = FSM_STATE_PASSTHROUGH;

                    /* Process current non-tag character */
                    if (c == '<') {
                        fsm->state = FSM_STATE_MATCHING_PREFIX;
                        fsm->buffer[0] = '<';
                        fsm->buf_len = 1;
                    } else if (c == 'E' || c == 'V') {
                        fsm->state = FSM_STATE_IN_ENTITY_TAG;
                        fsm->buffer[0] = c;
                        fsm->buf_len = 1;
                    } else {
                        append_to_out(out_buf, out_cap, &out_pos, c);
                    }
                }
                break;
        }
    }

    out_buf[out_pos] = '\0';
    return out_pos;
}
