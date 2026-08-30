#ifndef AEGIS_SSE_PARSER_H
#define AEGIS_SSE_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include "stream_fsm.h"
#include "arena.h"

typedef struct {
    stream_fsm_t fsm;
    arena_t *arena;
    bool is_done;
} sse_stream_handler_t;

/* Initialize SSE stream handler */
void sse_stream_handler_init(sse_stream_handler_t *handler, const session_vault_t *session, arena_t *arena);

/* Process an incoming SSE data chunk (which may contain one or multiple "data: ..." lines)
   Writes the rewritten SSE data chunk to out_buf. Returns bytes written. */
size_t sse_stream_process_data(sse_stream_handler_t *handler, 
                               const char *in_raw, 
                               size_t in_len, 
                               char *out_buf, 
                               size_t out_cap);

#endif /* AEGIS_SSE_PARSER_H */
