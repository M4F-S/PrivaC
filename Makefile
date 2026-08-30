CC ?= clang
CFLAGS ?= -Wall -Wextra -std=c11 -O3 -Iinclude -Ideps -pthread
LDFLAGS ?= -pthread -lm

SRC_CORE = src/arena.c src/vault.c src/symbolic_vm.c src/mask_engine.c src/stream_fsm.c src/sse_parser.c src/server.c
SRC_DEPS = deps/yyjson/yyjson.c deps/tinyexpr/tinyexpr.c

OBJ_CORE = $(SRC_CORE:.c=.o)
OBJ_DEPS = $(SRC_DEPS:.c=.o)

BIN_DIR = bin

all: $(BIN_DIR)/privac

$(BIN_DIR)/privac: src/main.o $(OBJ_CORE) $(OBJ_DEPS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/test_arena: tests/test_arena.o src/arena.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/test_vault: tests/test_vault.o src/vault.o src/arena.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/test_math_vm: tests/test_math_vm.o src/symbolic_vm.o src/vault.o src/arena.o deps/tinyexpr/tinyexpr.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/test_mask: tests/test_mask.o src/mask_engine.o src/vault.o src/arena.o deps/yyjson/yyjson.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/test_stream_fsm: tests/test_stream_fsm.o src/stream_fsm.o src/symbolic_vm.o src/vault.o src/arena.o deps/tinyexpr/tinyexpr.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/test_e2e_mock: tests/test_e2e_mock.o src/sse_parser.o src/stream_fsm.o src/symbolic_vm.o src/mask_engine.o src/vault.o src/arena.o deps/yyjson/yyjson.o deps/tinyexpr/tinyexpr.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/benchmark: tests/benchmark.o src/symbolic_vm.o src/mask_engine.o src/vault.o src/arena.o deps/yyjson/yyjson.o deps/tinyexpr/tinyexpr.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(BIN_DIR)/test_arena $(BIN_DIR)/test_vault $(BIN_DIR)/test_math_vm $(BIN_DIR)/test_mask $(BIN_DIR)/test_stream_fsm $(BIN_DIR)/test_e2e_mock
	@echo "\n================= RUNNING TEST SUITE ================="
	@./$(BIN_DIR)/test_arena
	@./$(BIN_DIR)/test_vault
	@./$(BIN_DIR)/test_math_vm
	@./$(BIN_DIR)/test_mask
	@./$(BIN_DIR)/test_stream_fsm
	@./$(BIN_DIR)/test_e2e_mock
	@echo "================= ALL TESTS PASSED! ==================\n"

sanitize:
	$(MAKE) clean
	$(MAKE) test CFLAGS="-Wall -Wextra -std=c11 -g -fsanitize=address,undefined -Iinclude -Ideps -pthread" LDFLAGS="-fsanitize=address,undefined -pthread -lm"
	$(MAKE) clean

bench: $(BIN_DIR)/benchmark
	@./$(BIN_DIR)/benchmark

clean:
	rm -rf $(BIN_DIR) src/*.o tests/*.o deps/yyjson/*.o deps/tinyexpr/*.o

.PHONY: all test sanitize bench clean
