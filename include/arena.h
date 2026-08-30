#ifndef AEGIS_ARENA_H
#define AEGIS_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define AEGIS_DEFAULT_ARENA_SIZE (128 * 1024) /* 128 KB */
#define AEGIS_ALIGNMENT 16

typedef struct arena_block {
    struct arena_block *next;
    size_t capacity;
    size_t offset;
    size_t _pad;
    _Alignas(AEGIS_ALIGNMENT) uint8_t data[];
} arena_block_t;

typedef struct {
    arena_block_t *head;
    arena_block_t *current;
    size_t default_block_size;
    size_t total_allocated;
} arena_t;

/* Initialize an arena with a default block size */
void arena_init(arena_t *arena, size_t default_block_size);

/* Allocate aligned memory from the arena */
void *arena_alloc(arena_t *arena, size_t size);

/* Allocate zeroed memory from the arena */
void *arena_calloc(arena_t *arena, size_t count, size_t size);

/* Duplicate a null-terminated string inside the arena */
char *arena_strdup(arena_t *arena, const char *str);

/* Duplicate a string of length n inside the arena */
char *arena_strndup(arena_t *arena, const char *str, size_t n);

/* Reset the arena (keeps blocks allocated, resets offsets) */
void arena_reset(arena_t *arena);

/* Free all underlying blocks */
void arena_destroy(arena_t *arena);

#endif /* AEGIS_ARENA_H */
