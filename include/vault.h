#ifndef PRIVAC_VAULT_H
#define PRIVAC_VAULT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "arena.h"

#define PRIVAC_MAX_SESSION_ID_LEN 64
#define PRIVAC_MAX_ORIGINAL_LEN 128
#define PRIVAC_MAX_SURROGATE_LEN 64
#define PRIVAC_MAX_ENTRIES_PER_SESSION 256
#define PRIVAC_MAX_SESSIONS 1024
#define PRIVAC_SESSION_TTL_SEC 1800 /* 30 minutes */

typedef enum {
    NUM_FMT_RAW_INT,
    NUM_FMT_RAW_FLOAT,
    NUM_FMT_CURRENCY_USD, /* $1,250,000.00 or $1,250,000 */
    NUM_FMT_PERCENTAGE,   /* 22% or 22.5% */
} number_format_t;

typedef enum {
    ENTRY_TYPE_STRING_ENTITY,
    ENTRY_TYPE_NUMERIC_VAR,
    ENTRY_TYPE_TYPE_PRESERVING_INT,
    ENTRY_TYPE_TYPE_PRESERVING_FLOAT
} entry_type_t;

typedef struct {
    char original_str[PRIVAC_MAX_ORIGINAL_LEN];
    char surrogate_str[PRIVAC_MAX_SURROGATE_LEN];
    double original_numeric;
    double surrogate_numeric;
    number_format_t num_format;
    entry_type_t type;
    int decimals;
    bool has_commas;
} vault_entry_t;

typedef struct session_vault {
    char session_id[PRIVAC_MAX_SESSION_ID_LEN];
    vault_entry_t entries[PRIVAC_MAX_ENTRIES_PER_SESSION];
    size_t count;
    uint64_t last_active_ts;
    bool is_locked; /* mlock active */
    arena_t arena;
    struct session_vault *next;
} session_vault_t;

typedef struct {
    session_vault_t *sessions[PRIVAC_MAX_SESSIONS];
    pthread_mutex_t lock;
    size_t active_sessions;
} vault_manager_t;

/* Initialize global vault manager */
void vault_manager_init(vault_manager_t *vm);

/* Destroy all sessions and securely wipe memory */
void vault_manager_destroy(vault_manager_t *vm);

/* Acquire or create a session vault */
session_vault_t *vault_get_or_create_session(vault_manager_t *vm, const char *session_id);

/* Add or get an existing string entity mapping ("Alice" -> "Entity_A") */
const char *session_vault_add_entity(session_vault_t *session, const char *original, const char *category);

/* Add a numeric variable mapping ("$1,250,000", value 1250000.0 -> "VAR_1") */
const char *session_vault_add_numeric_var(session_vault_t *session, 
                                          const char *original_raw, 
                                          double value, 
                                          number_format_t fmt, 
                                          int decimals, 
                                          bool has_commas);

/* Add type-preserving numeric surrogate */
int64_t session_vault_add_int_surrogate(session_vault_t *session, int64_t original_val);
double session_vault_add_float_surrogate(session_vault_t *session, double original_val);

/* Lookup original string by surrogate ("Entity_A" -> "Alice") */
const char *session_vault_lookup_original_str(const session_vault_t *session, const char *surrogate);

/* Lookup numeric variable details by symbol ("VAR_1" -> value, format, etc.) */
const vault_entry_t *session_vault_lookup_var(const session_vault_t *session, const char *var_name);

/* Securely wipe and free an individual session */
void session_vault_wipe_and_free(session_vault_t *session);

/* Evict expired sessions */
void vault_manager_evict_expired(vault_manager_t *vm, uint64_t current_ts);

/* Defensive memory zeroization helper */
void privac_secure_zero(void *ptr, size_t len);

#endif /* PRIVAC_VAULT_H */
