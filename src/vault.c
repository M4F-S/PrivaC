#include "vault.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

void privac_secure_zero(void *ptr, size_t len) {
    if (!ptr || len == 0) return;
#if defined(__STDC_LIB_EXT1__)
    memset_s(ptr, len, 0, len);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    memset(ptr, 0, len);
    __asm__ __volatile__("" : : "r"(ptr) : "memory");
#else
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) {
        *p++ = 0;
    }
#endif
}

static uint32_t hash_str(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % PRIVAC_MAX_SESSIONS;
}

void vault_manager_init(vault_manager_t *vm) {
    if (!vm) return;
    memset(vm->sessions, 0, sizeof(vm->sessions));
    pthread_mutex_init(&vm->lock, NULL);
    vm->active_sessions = 0;
}

void vault_manager_destroy(vault_manager_t *vm) {
    if (!vm) return;
    pthread_mutex_lock(&vm->lock);
    for (size_t i = 0; i < PRIVAC_MAX_SESSIONS; i++) {
        session_vault_t *curr = vm->sessions[i];
        while (curr) {
            session_vault_t *next = curr->next;
            session_vault_wipe_and_free(curr);
            curr = next;
        }
        vm->sessions[i] = NULL;
    }
    vm->active_sessions = 0;
    pthread_mutex_unlock(&vm->lock);
    pthread_mutex_destroy(&vm->lock);
}

session_vault_t *vault_get_or_create_session(vault_manager_t *vm, const char *session_id) {
    if (!vm || !session_id || strlen(session_id) == 0) return NULL;

    pthread_mutex_lock(&vm->lock);
    uint32_t bucket = hash_str(session_id);
    session_vault_t *curr = vm->sessions[bucket];

    while (curr) {
        if (strncmp(curr->session_id, session_id, PRIVAC_MAX_SESSION_ID_LEN) == 0) {
            curr->last_active_ts = (uint64_t)time(NULL);
            pthread_mutex_unlock(&vm->lock);
            return curr;
        }
        curr = curr->next;
    }

    /* Allocate new session */
    session_vault_t *new_session = (session_vault_t *)calloc(1, sizeof(session_vault_t));
    if (!new_session) {
        pthread_mutex_unlock(&vm->lock);
        return NULL;
    }

    /* Attempt mlock for RAM memory locking */
    if (mlock(new_session, sizeof(session_vault_t)) == 0) {
        new_session->is_locked = true;
    } else {
        new_session->is_locked = false;
    }

    strncpy(new_session->session_id, session_id, PRIVAC_MAX_SESSION_ID_LEN - 1);
    new_session->count = 0;
    new_session->last_active_ts = (uint64_t)time(NULL);
    arena_init(&new_session->arena, 32 * 1024);

    new_session->next = vm->sessions[bucket];
    vm->sessions[bucket] = new_session;
    vm->active_sessions++;

    pthread_mutex_unlock(&vm->lock);
    return new_session;
}

const char *session_vault_add_entity(session_vault_t *session, const char *original, const char *category) {
    if (!session || !original) return original;

    /* Check if already mapped */
    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_STRING_ENTITY &&
            strcmp(session->entries[i].original_str, original) == 0) {
            return session->entries[i].surrogate_str;
        }
    }

    if (session->count >= PRIVAC_MAX_ENTRIES_PER_SESSION) {
        return original;
    }

    vault_entry_t *entry = &session->entries[session->count];
    strncpy(entry->original_str, original, PRIVAC_MAX_ORIGINAL_LEN - 1);
    entry->type = ENTRY_TYPE_STRING_ENTITY;

    /* Count existing entity entries to assign letter/number */
    size_t entity_idx = 0;
    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_STRING_ENTITY) {
            entity_idx++;
        }
    }

    if (category && strlen(category) > 0) {
        snprintf(entry->surrogate_str, PRIVAC_MAX_SURROGATE_LEN, "%s_%c", category, (char)('A' + (entity_idx % 26)));
    } else {
        snprintf(entry->surrogate_str, PRIVAC_MAX_SURROGATE_LEN, "Entity_%c", (char)('A' + (entity_idx % 26)));
    }

    session->count++;
    return entry->surrogate_str;
}

const char *session_vault_add_numeric_var(session_vault_t *session, 
                                          const char *original_raw, 
                                          double value, 
                                          number_format_t fmt, 
                                          int decimals, 
                                          bool has_commas) {
    if (!session || !original_raw) return original_raw;

    /* Check if already mapped */
    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_NUMERIC_VAR &&
            strcmp(session->entries[i].original_str, original_raw) == 0) {
            return session->entries[i].surrogate_str;
        }
    }

    if (session->count >= PRIVAC_MAX_ENTRIES_PER_SESSION) {
        return original_raw;
    }

    vault_entry_t *entry = &session->entries[session->count];
    strncpy(entry->original_str, original_raw, PRIVAC_MAX_ORIGINAL_LEN - 1);
    entry->original_numeric = value;
    entry->num_format = fmt;
    entry->decimals = decimals;
    entry->has_commas = has_commas;
    entry->type = ENTRY_TYPE_NUMERIC_VAR;

    size_t var_idx = 1;
    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_NUMERIC_VAR) {
            var_idx++;
        }
    }

    snprintf(entry->surrogate_str, PRIVAC_MAX_SURROGATE_LEN, "VAR_%zu", var_idx);
    session->count++;
    return entry->surrogate_str;
}

int64_t session_vault_add_int_surrogate(session_vault_t *session, int64_t original_val) {
    if (!session) return original_val;

    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_TYPE_PRESERVING_INT &&
            (int64_t)session->entries[i].original_numeric == original_val) {
            return (int64_t)session->entries[i].surrogate_numeric;
        }
    }

    if (session->count >= PRIVAC_MAX_ENTRIES_PER_SESSION) {
        return original_val;
    }

    vault_entry_t *entry = &session->entries[session->count];
    entry->type = ENTRY_TYPE_TYPE_PRESERVING_INT;
    entry->original_numeric = (double)original_val;
    
    int64_t surrogate = (original_val ^ 0x5DEECE66DL) % 900000;
    if (surrogate < 100000) surrogate += 100000;
    entry->surrogate_numeric = (double)surrogate;
    
    snprintf(entry->original_str, PRIVAC_MAX_ORIGINAL_LEN, "%lld", (long long)original_val);
    snprintf(entry->surrogate_str, PRIVAC_MAX_SURROGATE_LEN, "%lld", (long long)surrogate);

    session->count++;
    return surrogate;
}

double session_vault_add_float_surrogate(session_vault_t *session, double original_val) {
    if (!session) return original_val;

    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_TYPE_PRESERVING_FLOAT &&
            session->entries[i].original_numeric == original_val) {
            return session->entries[i].surrogate_numeric;
        }
    }

    if (session->count >= PRIVAC_MAX_ENTRIES_PER_SESSION) {
        return original_val;
    }

    vault_entry_t *entry = &session->entries[session->count];
    entry->type = ENTRY_TYPE_TYPE_PRESERVING_FLOAT;
    entry->original_numeric = original_val;
    
    double surrogate = (original_val > 0.0 && original_val <= 1.0) ? (original_val * 0.73 + 0.11) : (original_val * 1.37);
    entry->surrogate_numeric = surrogate;

    snprintf(entry->original_str, PRIVAC_MAX_ORIGINAL_LEN, "%.4f", original_val);
    snprintf(entry->surrogate_str, PRIVAC_MAX_SURROGATE_LEN, "%.4f", surrogate);

    session->count++;
    return surrogate;
}

const char *session_vault_lookup_original_str(const session_vault_t *session, const char *surrogate) {
    if (!session || !surrogate) return surrogate;
    for (size_t i = 0; i < session->count; i++) {
        if (strcmp(session->entries[i].surrogate_str, surrogate) == 0) {
            return session->entries[i].original_str;
        }
    }
    return surrogate;
}

const vault_entry_t *session_vault_lookup_var(const session_vault_t *session, const char *var_name) {
    if (!session || !var_name) return NULL;
    for (size_t i = 0; i < session->count; i++) {
        if (session->entries[i].type == ENTRY_TYPE_NUMERIC_VAR &&
            strcmp(session->entries[i].surrogate_str, var_name) == 0) {
            return &session->entries[i];
        }
    }
    return NULL;
}

void session_vault_wipe_and_free(session_vault_t *session) {
    if (!session) return;
    arena_destroy(&session->arena);
    
    if (session->is_locked) {
        munlock(session, sizeof(session_vault_t));
    }
    privac_secure_zero(session, sizeof(session_vault_t));
    free(session);
}

void vault_manager_evict_expired(vault_manager_t *vm, uint64_t current_ts) {
    if (!vm) return;
    pthread_mutex_lock(&vm->lock);
    for (size_t i = 0; i < PRIVAC_MAX_SESSIONS; i++) {
        session_vault_t **curr_ptr = &vm->sessions[i];
        while (*curr_ptr) {
            session_vault_t *s = *curr_ptr;
            if (current_ts > s->last_active_ts && (current_ts - s->last_active_ts) > PRIVAC_SESSION_TTL_SEC) {
                *curr_ptr = s->next;
                session_vault_wipe_and_free(s);
                vm->active_sessions--;
            } else {
                curr_ptr = &s->next;
            }
        }
    }
    pthread_mutex_unlock(&vm->lock);
}
