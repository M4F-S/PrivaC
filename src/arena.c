#include "arena.h"
#include <stdlib.h>
#include <string.h>

static inline size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

static arena_block_t *create_block(size_t capacity) {
    arena_block_t *block = (arena_block_t *)malloc(sizeof(arena_block_t) + capacity);
    if (!block) return NULL;
    block->next = NULL;
    block->capacity = capacity;
    block->offset = 0;
    return block;
}

void arena_init(arena_t *arena, size_t default_block_size) {
    if (!arena) return;
    if (default_block_size == 0) {
        default_block_size = AEGIS_DEFAULT_ARENA_SIZE;
    }
    arena->default_block_size = default_block_size;
    arena->head = create_block(default_block_size);
    arena->current = arena->head;
    arena->total_allocated = 0;
}

void *arena_alloc(arena_t *arena, size_t size) {
    if (!arena || size == 0) return NULL;

    size_t aligned_size = align_up(size, AEGIS_ALIGNMENT);

    if (!arena->current) {
        arena->head = create_block(arena->default_block_size > aligned_size ? arena->default_block_size : aligned_size);
        arena->current = arena->head;
        if (!arena->current) return NULL;
    }

    /* Check if current block has enough space */
    if (arena->current->offset + aligned_size <= arena->current->capacity) {
        void *ptr = &arena->current->data[arena->current->offset];
        arena->current->offset += aligned_size;
        arena->total_allocated += aligned_size;
        return ptr;
    }

    /* Allocate next block */
    size_t next_capacity = arena->default_block_size;
    if (aligned_size > next_capacity) {
        next_capacity = aligned_size;
    }

    arena_block_t *next_block = create_block(next_capacity);
    if (!next_block) return NULL;

    arena->current->next = next_block;
    arena->current = next_block;

    void *ptr = &arena->current->data[arena->current->offset];
    arena->current->offset += aligned_size;
    arena->total_allocated += aligned_size;
    return ptr;
}

void *arena_calloc(arena_t *arena, size_t count, size_t size) {
    size_t total = count * size;
    void *ptr = arena_alloc(arena, total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

char *arena_strdup(arena_t *arena, const char *str) {
    if (!arena || !str) return NULL;
    size_t len = strlen(str);
    char *copy = (char *)arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

char *arena_strndup(arena_t *arena, const char *str, size_t n) {
    if (!arena || !str) return NULL;
    size_t len = strnlen(str, n);
    char *copy = (char *)arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

void arena_reset(arena_t *arena) {
    if (!arena) return;
    arena_block_t *block = arena->head;
    while (block) {
        block->offset = 0;
        block = block->next;
    }
    arena->current = arena->head;
    arena->total_allocated = 0;
}

void arena_destroy(arena_t *arena) {
    if (!arena) return;
    arena_block_t *block = arena->head;
    while (block) {
        arena_block_t *next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
    arena->current = NULL;
    arena->total_allocated = 0;
}
