#include "sse_parser.h"
#include "../deps/yyjson/yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sse_stream_handler_init(sse_stream_handler_t *handler, const session_vault_t *session, arena_t *arena) {
    if (!handler) return;
    stream_fsm_init(&handler->fsm, session);
    handler->arena = arena;
    handler->is_done = false;
}

size_t sse_stream_process_data(sse_stream_handler_t *handler, 
                               const char *in_raw, 
                               size_t in_len, 
                               char *out_buf, 
                               size_t out_cap) {
    if (!handler || !in_raw || in_len == 0 || !out_buf || out_cap == 0) return 0;

    size_t out_pos = 0;
    const char *line = in_raw;
    const char *end = in_raw + in_len;

    while (line < end) {
        const char *next_nl = (const char *)memchr(line, '\n', (size_t)(end - line));
        size_t line_len = next_nl ? (size_t)(next_nl - line) : (size_t)(end - line);

        /* Skip empty lines (SSE event separators) */
        if (line_len == 0 || (line_len == 1 && line[0] == '\r')) {
            line = next_nl ? next_nl + 1 : end;
            continue;
        }

        /* Check for "data: " prefix */
        if (line_len >= 6 && strncmp(line, "data: ", 6) == 0) {
            const char *data_payload = line + 6;
            size_t payload_len = line_len - 6;
            if (payload_len > 0 && data_payload[payload_len - 1] == '\r') {
                payload_len--;
            }

            /* Check for [DONE] */
            if (payload_len == 6 && strncmp(data_payload, "[DONE]", 6) == 0) {
                /* Flush any remaining FSM buffer */
                char flush_buf[512];
                size_t flushed = stream_fsm_flush(&handler->fsm, flush_buf, sizeof(flush_buf));
                if (flushed > 0) {
                    char syn_chunk[1024];
                    int n = snprintf(syn_chunk, sizeof(syn_chunk), 
                                     "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"%s\"}}]}\n\n", 
                                     flush_buf);
                    if (n > 0 && out_pos + (size_t)n < out_cap) {
                        memcpy(&out_buf[out_pos], syn_chunk, (size_t)n);
                        out_pos += (size_t)n;
                    }
                }

                if (out_pos + 14 < out_cap) {
                    memcpy(&out_buf[out_pos], "data: [DONE]\n\n", 14);
                    out_pos += 14;
                }
                handler->is_done = true;
            } else {
                /* Parse SSE JSON chunk */
                yyjson_doc *doc = yyjson_read_opts((char *)data_payload, payload_len, 0, NULL, NULL);
                if (doc) {
                    yyjson_val *root = yyjson_doc_get_root(doc);
                    yyjson_val *choices = yyjson_obj_get(root, "choices");
                    if (yyjson_is_arr(choices) && yyjson_arr_size(choices) > 0) {
                        yyjson_val *choice0 = yyjson_arr_get(choices, 0);
                        yyjson_val *delta = yyjson_obj_get(choice0, "delta");
                        yyjson_val *content = yyjson_obj_get(delta, "content");

                        if (yyjson_is_str(content)) {
                            const char *token_str = yyjson_get_str(content);
                            size_t token_len = yyjson_get_len(content);

                            char fsm_out[1024];
                            size_t emitted = stream_fsm_process_chunk(&handler->fsm, token_str, token_len, fsm_out, sizeof(fsm_out));

                            if (emitted > 0) {
                                yyjson_mut_doc *mut_doc = yyjson_doc_mut_copy(doc, NULL);
                                if (mut_doc) {
                                    yyjson_mut_val *m_root = yyjson_mut_doc_get_root(mut_doc);
                                    yyjson_mut_val *m_choices = yyjson_mut_obj_get(m_root, "choices");
                                    yyjson_mut_val *m_choice0 = yyjson_mut_arr_get(m_choices, 0);
                                    yyjson_mut_val *m_delta = yyjson_mut_obj_get(m_choice0, "delta");
                                    yyjson_mut_val *m_content = yyjson_mut_obj_get(m_delta, "content");
                                    if (m_content) {
                                        yyjson_mut_set_str(m_content, fsm_out);
                                    }

                                    size_t json_out_len = 0;
                                    char *json_str = yyjson_mut_write(mut_doc, 0, &json_out_len);
                                    if (json_str) {
                                        if (out_pos + json_out_len + 8 < out_cap) {
                                            memcpy(&out_buf[out_pos], "data: ", 6);
                                            out_pos += 6;
                                            memcpy(&out_buf[out_pos], json_str, json_out_len);
                                            out_pos += json_out_len;
                                            memcpy(&out_buf[out_pos], "\n\n", 2);
                                            out_pos += 2;
                                        }
                                        free(json_str);
                                    }
                                    yyjson_mut_doc_free(mut_doc);
                                }
                            }
                        }
                    }
                    yyjson_doc_free(doc);
                } else {
                    /* Non-JSON or passthrough data line */
                    if (out_pos + line_len + 2 < out_cap) {
                        memcpy(&out_buf[out_pos], line, line_len);
                        out_pos += line_len;
                        out_buf[out_pos++] = '\n';
                    }
                }
            }
        } else {
            /* Other SSE headers/events */
            if (out_pos + line_len + 1 < out_cap) {
                memcpy(&out_buf[out_pos], line, line_len);
                out_pos += line_len;
                out_buf[out_pos++] = '\n';
            }
        }

        line = next_nl ? next_nl + 1 : end;
    }

    out_buf[out_pos] = '\0';
    return out_pos;
}
