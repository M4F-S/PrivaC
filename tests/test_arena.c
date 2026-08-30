#include "../include/arena.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("Running test_arena...\n");

    arena_t arena;
    arena_init(&arena, 1024);

    void *p1 = arena_alloc(&arena, 128);
    assert(p1 != NULL);
    assert(((uintptr_t)p1 % PRIVAC_ALIGNMENT) == 0);

    void *p2 = arena_alloc(&arena, 256);
    assert(p2 != NULL);
    assert(((uintptr_t)p2 % PRIVAC_ALIGNMENT) == 0);

    char *str = arena_strdup(&arena, "Hello Aegis Pure C Memory Arena");
    assert(strcmp(str, "Hello Aegis Pure C Memory Arena") == 0);

    /* Allocate larger than default block size to test block chaining */
    void *p_large = arena_alloc(&arena, 4096);
    assert(p_large != NULL);

    arena_reset(&arena);
    assert(arena.current == arena.head);
    assert(arena.total_allocated == 0);

    char *str2 = arena_strdup(&arena, "Post reset allocation");
    assert(strcmp(str2, "Post reset allocation") == 0);

    arena_destroy(&arena);
    assert(arena.head == NULL);

    printf("✓ test_arena passed successfully!\n");
    return 0;
}
