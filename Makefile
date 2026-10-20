# build/ is the C library and the Unity binary. ui/dist/ is the page Tauri embeds.
# src-tauri/target/ is Cargo's tree. dist/pavillion is the program make build publishes.
# dist/pavillion-mini is make mini: a size build, then UPX. It is not the rpm.
# The shell links the archive from src-tauri/build.rs.

BUILD_DIR := build
DIST_DIR := dist
LIB := $(BUILD_DIR)/libpavillion_core.a
TEST_BIN := $(BUILD_DIR)/core_test
RELEASE_BIN := src-tauri/target/release/pavillion
MINI_CMAKE_DIR := build-mini
MINI_BIN := src-tauri/target/mini/pavillion
MINI_DIST := $(DIST_DIR)/pavillion-mini
# Override with `make mini UPX_FLAGS='--ultra-brute'` if you want UPX to search longer.
UPX_FLAGS ?= --best --lzma
MINI_RUSTFLAGS := $(strip $(RUSTFLAGS) -C force-unwind-tables=no -C link-arg=-Wl,--gc-sections -C link-arg=-Wl,--as-needed -C link-arg=-Wl,-O1)
# Wall clock from when make read this file, so lib and the rest of the target count.
BUILD_START := $(shell date +%s)
PRINT_ELAPSED = elapsed=$$(( $$(date +%s) - $(BUILD_START) )); printf 'build time    %dh%dm%ds\n' $$((elapsed / 3600)) $$(((elapsed % 3600) / 60)) $$((elapsed % 60))

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
# srv_lookup (G Lock): libresolv on Linux and macOS, dnsapi on Windows.
ifeq ($(OS),Windows_NT)
PLATFORM_TEST_LIBS := -ldnsapi
else ifneq ($(filter Linux Darwin,$(UNAME_S)),)
PLATFORM_TEST_LIBS := -lresolv
else
PLATFORM_TEST_LIBS :=
endif

.PHONY: all lib dev build test clean mini it-up it-seed it-down it-reset test-it

all: lib

lib: $(LIB)

$(LIB): core/CMakeLists.txt core/include/pavillion.h core/include/pav_config.h \
		core/include/pav_cfg.h core/include/pav_store.h core/include/pav_platform.h \
		core/src/pavillion.c core/src/logring.c core/src/logring.h \
		core/src/dispatch.c core/src/dispatch.h \
		core/src/window_state.c core/src/window_state.h \
		core/src/protocol.c core/src/themes.c core/src/themes.h core/src/cli.c \
		core/src/cfg.c core/src/config.c core/src/config_prefs.c \
		core/src/config_fs.c core/src/config_internal.h core/src/store.c \
		core/src/db.c core/src/db_writer.c core/src/db_readpool.c \
		core/src/migrate.c core/src/migrations_embed.h \
		core/src/migrations/001_accounts.sql core/embed_sql.cmake \
		core/src/logdb.c core/include/pav_logdb.h core/src/migrations/log/001_log.sql \
		core/include/pav_db.h \
		core/src/activity.c core/include/pav_activity.h \
		core/platform/platform_internal.h core/platform/posix/posix.c \
		core/platform/linux/linux.c core/platform/linux/secrets.c \
		core/platform/stronghold/Cargo.toml core/platform/stronghold/src/lib.rs \
		core/src/secret.c core/include/pav_secret.h \
		core/src/auth_basic.c core/include/pav_auth.h \
		core/src/account.c core/include/account.h \
		core/src/net.c core/include/pav_net.h \
	core/src/jmap.c core/include/pav_jmap.h \
	core/src/account_test.c
	cmake -S core -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

$(TEST_BIN): $(LIB) $(TEST_SRC) $(PLATFORM_TEST_SRC) $(UNITY_DIR)/unity.c $(UNITY_DIR)/unity.h \
		$(UNITY_DIR)/unity_internals.h
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -pthread -DUNITY_INCLUDE_DOUBLE \
		-Icore/include -Icore/src -I$(UNITY_DIR) $(PLATFORM_TEST_INC) $(CFLAGS) \
		-o $@ $(TEST_SRC) $(UNITY_DIR)/unity.c $(PLATFORM_TEST_SRC) $(LIB) \
		-ljansson -lsqlite3 $(PLATFORM_TEST_LIBS) $(LDFLAGS) \
		$(shell pkg-config --libs libsecret-1 2>/dev/null) \
		$(shell pkg-config --libs libcurl 2>/dev/null) \
		$(if $(wildcard core/platform/stronghold/target/release/libpav_stronghold.a),core/platform/stronghold/target/release/libpav_stronghold.a -ldl -lm,)

# dev, build, and mini add one to PAVILLION_BUILD before they compile.
# test does not. The shell and the page read that file. The file is local and
# ignored by git; a fresh clone gets 1 from the rule below.
PAVILLION_BUILD:
	printf '1\n' >$@

dev: lib PAVILLION_BUILD
	bash scripts/bump-build.sh
	npm --prefix ui install
	npm --prefix ui exec -- tauri dev $(if $(DEV_DIR),-- -- "$(DEV_DIR)",)

build: lib PAVILLION_BUILD
	bash scripts/bump-build.sh
	npm --prefix ui install
	npm --prefix ui exec -- tauri build
	mkdir -p $(DIST_DIR)
	cp -f $(RELEASE_BIN) $(DIST_DIR)/pavillion
	@$(PRINT_ELAPSED)

# Same program as make build, aimed at the smallest file. Uses build-mini/ and
# target/mini/ so it does not rewrite build/ or dist/pavillion.
mini: PAVILLION_BUILD
	@command -v upx >/dev/null 2>&1 || { printf '%s\n' "make mini needs upx" >&2; exit 1; }
	@command -v strip >/dev/null 2>&1 || { printf '%s\n' "make mini needs strip" >&2; exit 1; }
	@command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1 || { printf '%s\n' "make mini needs ImageMagick" >&2; exit 1; }
	bash scripts/bump-build.sh
	npm --prefix ui install
	PAVILLION_CMAKE_DIR=$(MINI_CMAKE_DIR) PAVILLION_SIZE=1 \
		RUSTFLAGS="$(MINI_RUSTFLAGS)" \
		npm --prefix ui exec -- tauri build --config "$(CURDIR)/src-tauri/tauri.mini.conf.json" -- --profile mini --no-default-features
	strip --strip-unneeded $(MINI_BIN)
	@readelf -d $(MINI_BIN) | grep -q 'Shared library: \[libwebkit2gtk-4.1.so.0\]' \
		|| { printf '%s\n' "mini binary does not dynamically need libwebkit2gtk" >&2; exit 1; }
	@grep -a -q -F '.pav-root .nav-mark.is-shown' $(MINI_BIN) \
		|| { printf '%s\n' "mini binary is missing the raw stylesheet" >&2; exit 1; }
	mkdir -p $(DIST_DIR)
	rm -f $(MINI_DIST)
	upx $(UPX_FLAGS) --no-backup -o $(MINI_DIST) $(MINI_BIN)
	upx -t -q $(MINI_DIST)
	$(MINI_DIST) --version
	$(MINI_DIST) --help >/dev/null
	@echo "mini unpacked $$(stat -c%s $(MINI_BIN)) bytes"
	@echo "mini packed   $$(stat -c%s $(MINI_DIST)) bytes"
	@if [ -f $(DIST_DIR)/pavillion ]; then echo "make build    $$(stat -c%s $(DIST_DIR)/pavillion) bytes"; fi
	@$(PRINT_ELAPSED)

test: PAVILLION_BUILD $(TEST_BIN)
	bash scripts/run-test.sh

# Integration tests against the Stalwart container (PAVILLION_IT=1).
# Skipped until M-2 (account.test) exists; for now it only checks the
# container is reachable.
# Stalwart test container (tests/stalwart/). The admin password is test-only;
# PAV_STALWART_ADMIN_PW overrides it. The container runs as the invoking user
# so build/stalwart-data stays owned by them.
PAV_STALWART_ADMIN_PW ?= pavtest-admin
STALWART_ENV = PAV_UID=$(shell id -u) PAV_GID=$(shell id -g) PAV_STALWART_ADMIN_PW='$(PAV_STALWART_ADMIN_PW)'
STALWART_COMPOSE = docker compose -f tests/stalwart/compose.yaml

it-up:
	bash tests/support/testca.sh build/stalwart-tls
	mkdir -p build/stalwart-data
	$(STALWART_ENV) $(STALWART_COMPOSE) up -d
	$(MAKE) it-seed

it-seed:
	$(STALWART_ENV) PAV_STALWART_HOST=localhost PAV_STALWART_PORT=1443 bash tests/stalwart/seed.sh

it-down:
	$(STALWART_ENV) $(STALWART_COMPOSE) down --remove-orphans || true

it-reset: it-down
	rm -rf build/stalwart-data

test-it:
	@if [ "${PAVILLION_IT}" != "1" ]; then \t	echo "set PAVILLION_IT=1 to run integration tests" >&2; \t	exit 0; \tfi
	@echo "integration tests skipped until M-2 (account.test)"

clean:
	rm -rf $(BUILD_DIR) $(MINI_CMAKE_DIR) $(DIST_DIR)
