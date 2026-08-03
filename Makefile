CC ?= cc
CPPFLAGS := -Iinclude
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD_DIR := make-build
FLASH_TEST_BINARY := $(BUILD_DIR)/test_flash_sim
IMAGE_TEST_BINARY := $(BUILD_DIR)/test_image
IMAGE_SOURCES := src/flash_sim.c src/crc32.c src/sha256.c src/image.c src/boot_selector.c

.PHONY: all test sanitizers clean

all: $(FLASH_TEST_BINARY) $(IMAGE_TEST_BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(FLASH_TEST_BINARY): src/flash_sim.c tests/test_flash_sim.c include/flash_sim.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/flash_sim.c tests/test_flash_sim.c -o $(FLASH_TEST_BINARY)

$(IMAGE_TEST_BINARY): $(IMAGE_SOURCES) tests/test_image.c include/flash_sim.h include/crc32.h include/sha256.h include/image.h include/boot_selector.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(IMAGE_SOURCES) tests/test_image.c -o $(IMAGE_TEST_BINARY)

test: $(FLASH_TEST_BINARY) $(IMAGE_TEST_BINARY)
	./$(FLASH_TEST_BINARY)
	./$(IMAGE_TEST_BINARY)

sanitizers: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
sanitizers: clean test

clean:
	rm -rf $(BUILD_DIR)
