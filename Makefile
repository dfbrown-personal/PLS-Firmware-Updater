CC ?= cc
CPPFLAGS := -Iinclude
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD_DIR := make-build
TEST_BINARY := $(BUILD_DIR)/test_flash_sim

.PHONY: all test sanitizers clean

all: $(TEST_BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BINARY): src/flash_sim.c tests/test_flash_sim.c include/flash_sim.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/flash_sim.c tests/test_flash_sim.c -o $(TEST_BINARY)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

sanitizers: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
sanitizers: clean test

clean:
	rm -rf $(BUILD_DIR)
