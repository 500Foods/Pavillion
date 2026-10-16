# build/ is the C library and the Unity binary. ui/dist/ is the page Tauri embeds.
# src-tauri/target/ is Cargo's tree. dist/pavillion is the program make build publishes.
# The shell links the archive from src-tauri/build.rs.

BUILD_DIR := build
DIST_DIR := dist
LIB := $(BUILD_DIR)/libpavillion_core.a
TEST_BIN := $(BUILD_DIR)/core_test
RELEASE_BIN := src-tauri/target/release/pavillion

UNITY_DIR := tests/unity/framework/src
TEST_SRC := tests/unity/src/core_test.c
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
PLATFORM_TEST_SRC := core/platform/fake/fake.c
PLATFORM_TEST_INC := -Icore/platform
else
PLATFORM_TEST_SRC :=
PLATFORM_TEST_INC :=
endif

.PHONY: all lib dev build test clean

all: lib

lib: $(LIB)

$(LIB): core/CMakeLists.txt core/include/pavillion.h core/include/pav_config.h \
		core/include/pav_cfg.h core/include/pav_store.h core/include/pav_platform.h \
		core/src/pavillion.c core/src/cfg.c core/src/config.c core/src/config_prefs.c \
		core/src/config_fs.c core/src/config_internal.h core/src/store.c \
		core/platform/platform_internal.h core/platform/posix/posix.c \
		core/platform/linux/linux.c
	cmake -S core -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

$(TEST_BIN): $(LIB) $(TEST_SRC) $(PLATFORM_TEST_SRC) $(UNITY_DIR)/unity.c $(UNITY_DIR)/unity.h \
		$(UNITY_DIR)/unity_internals.h
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -pthread -DUNITY_INCLUDE_DOUBLE \
		-Icore/include -I$(UNITY_DIR) $(PLATFORM_TEST_INC) $(CFLAGS) \
		-o $@ $(TEST_SRC) $(UNITY_DIR)/unity.c $(PLATFORM_TEST_SRC) $(LIB) \
		-ljansson -lsqlite3 $(LDFLAGS)

dev: lib
	npm --prefix ui install
	npm --prefix ui exec -- tauri dev $(if $(DEV_DIR),-- -- "$(DEV_DIR)",)

build: lib
	npm --prefix ui install
	npm --prefix ui exec -- tauri build
	mkdir -p $(DIST_DIR)
	cp -f $(RELEASE_BIN) $(DIST_DIR)/pavillion

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
