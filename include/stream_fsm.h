#ifndef AEGIS_STREAM_FSM_H
#define AEGIS_STREAM_FSM_H

#include <stddef.h>
#include <stdbool.h>
#include "vault.h"

#define AEGIS_STREAM_FSM_BUF_SIZE 1024

typedef enum {
    FSM_STATE_PASSTHROUGH,
    FSM_STATE_MATCHING_PREFIX,
    FSM_STATE_IN_CALC_EXPR,
    FSM_STATE_IN_ENTITY_TAG
} stream_fsm_state_t;

typedef struct {
    stream_fsm_state_t state;
    char buffer[AEGIS_STREAM_FSM_BUF_SIZE];
    size_t buf_len;
    char prefix_expected[32];
    size_t prefix_len;
    const session_vault_t *session;
} stream_fsm_t;

/* Initialize streaming FSM with session context */
void stream_fsm_init(stream_fsm_t *fsm, const session_vault_t *session);

/* Reset FSM state between streams */
void stream_fsm_reset(stream_fsm_t *fsm);

/* Process an incoming chunk of delta content. 
   Appends emitted / unmasked characters to out_buf (up to out_cap).
   Returns number of bytes written to out_buf. */
size_t stream_fsm_process_chunk(stream_fsm_t *fsm, 
                                const char *in_chunk, 
                                size_t in_len, 
                                char *out_buf, 
                                size_t out_cap);

/* Flush any remaining buffered characters at end of stream */
size_t stream_fsm_flush(stream_fsm_t *fsm, char *out_buf, size_t out_cap);

#endif /* AEGIS_STREAM_FSM_H */
