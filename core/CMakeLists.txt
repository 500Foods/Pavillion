cmake_minimum_required(VERSION 3.16)
project(pavillion_core C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MIGRATION_EMBED ${CMAKE_CURRENT_BINARY_DIR}/migrations_embed.c)
file(GLOB MIGRATION_SQL CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/migrations/*.sql")
add_custom_command(
    OUTPUT ${MIGRATION_EMBED}
    COMMAND ${CMAKE_COMMAND}
        -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/src/migrations
        -DOUT=${MIGRATION_EMBED}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_sql.cmake
    DEPENDS ${MIGRATION_SQL} ${CMAKE_CURRENT_SOURCE_DIR}/embed_sql.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/src/migrations_embed.h
    VERBATIM
)

set(LOG_MIGRATION_EMBED ${CMAKE_CURRENT_BINARY_DIR}/log_migrations_embed.c)
file(GLOB LOG_MIGRATION_SQL CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/migrations/log/*.sql")
add_custom_command(
    OUTPUT ${LOG_MIGRATION_EMBED}
    COMMAND ${CMAKE_COMMAND}
        -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/src/migrations/log
        -DOUT=${LOG_MIGRATION_EMBED}
        -DFUNC=pav_log_migration_table
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_sql.cmake
    DEPENDS ${LOG_MIGRATION_SQL} ${CMAKE_CURRENT_SOURCE_DIR}/embed_sql.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/src/migrations_embed.h
    VERBATIM
)

add_library(pavillion_core STATIC
    src/pavillion.c
    src/logring.c
    src/dispatch.c
    src/window_state.c
    src/protocol.c
    src/themes.c
    src/cli.c
    src/cfg.c
    src/config.c
    src/config_prefs.c
    src/config_fs.c
    src/store.c
    src/db.c
    src/db_writer.c
    src/db_readpool.c
    src/migrate.c
    ${MIGRATION_EMBED}
    ${LOG_MIGRATION_EMBED}
    src/logdb.c
    src/activity.c
    src/blobs.c
    src/cache_usage.c
    src/secret.c
    src/auth_basic.c
    src/account.c
     src/net.c
     src/jmap.c
     src/fts.c
     src/sync.c
     src/message_list.c
     src/mime.c
     src/body.c
     src/account_test.c
     platform/posix/posix.c)
target_include_directories(pavillion_core PUBLIC include)
target_include_directories(pavillion_core PRIVATE platform src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(pavillion_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(pavillion_core PUBLIC pthread)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pavillion_core PRIVATE platform/linux/linux.c platform/posix/srv_resolv.c)
    target_link_libraries(pavillion_core PUBLIC resolv)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCURL libcurl)
    if(LIBCURL_FOUND)
        target_link_libraries(pavillion_core PUBLIC ${LIBCURL_LIBRARIES})
        target_include_directories(pavillion_core PRIVATE ${LIBCURL_INCLUDE_DIRS})
    endif()
    pkg_check_modules(LIBSECRET libsecret-1)
    if(LIBSECRET_FOUND)
        target_sources(pavillion_core PRIVATE platform/linux/secrets.c)
        target_compile_definitions(pavillion_core PRIVATE PAV_HAVE_LIBSECRET=1)
        target_include_directories(pavillion_core PRIVATE ${LIBSECRET_INCLUDE_DIRS})
        target_link_libraries(pavillion_core PUBLIC ${LIBSECRET_LIBRARIES})
        set(STRONGHOLD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/stronghold)
        set(STRONGHOLD_LIB ${STRONGHOLD_DIR}/target/release/libpav_stronghold.a)
        add_custom_command(
            OUTPUT ${STRONGHOLD_LIB}
            COMMAND cargo build --release --manifest-path ${STRONGHOLD_DIR}/Cargo.toml
            DEPENDS ${STRONGHOLD_DIR}/Cargo.toml ${STRONGHOLD_DIR}/src/lib.rs
            COMMENT "Building Stronghold static library"
            VERBATIM
        )
        add_custom_target(pav_stronghold DEPENDS ${STRONGHOLD_LIB})
        add_dependencies(pavillion_core pav_stronghold)
        target_link_libraries(pavillion_core PUBLIC ${STRONGHOLD_LIB} dl m)
    endif()
else()
    # No macOS backend until G-6. The core CI job links the fake as its host.
    target_sources(pavillion_core PRIVATE platform/fake/fake.c platform/fake/host.c)
    # srv_lookup (G Lock): libresolv on macOS, dnsapi on Windows (MinGW).
    if(APPLE)
        target_sources(pavillion_core PRIVATE platform/posix/srv_resolv.c)
        target_link_libraries(pavillion_core PUBLIC resolv)
    elseif(WIN32)
        target_sources(pavillion_core PRIVATE platform/windows/srv_dnsapi.c)
        target_link_libraries(pavillion_core PUBLIC dnsapi)
    endif()
endif()

# GMime 3. Fedora's gmime-devel is 2.6; the 3.x package is gmime30-devel
# (pkg-config gmime-3.0). This machine keeps 3.2.15 under ~/.local.
if(EXISTS "$ENV{HOME}/.local/lib/pkgconfig/gmime-3.0.pc")
    set(ENV{PKG_CONFIG_PATH} "$ENV{HOME}/.local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GMIME REQUIRED gmime-3.0)
target_include_directories(pavillion_core PRIVATE ${GMIME_INCLUDE_DIRS})
target_compile_options(pavillion_core PRIVATE ${GMIME_CFLAGS_OTHER})
target_link_directories(pavillion_core PUBLIC ${GMIME_LIBRARY_DIRS})
target_link_libraries(pavillion_core PUBLIC ${GMIME_LIBRARIES})

# N-2. Lexbor v3.0.0 is vendored. The static archive is linked whole into the
# final binary (Makefile and src-tauri/build.rs). It is not merged into
# libpavillion_core.a: lexbor's objects share basenames across modules.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS_CPP OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_WITH_ASAN OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_WITH_MSAN OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_WITH_FUZZER OFF CACHE BOOL "" FORCE)
set(LEXBOR_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
set(LEXBOR_WITHOUT_THREADS ON CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_WASM OFF CACHE BOOL "" FORCE)
set(LEXBOR_MAKE_PACKAGES_FILES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/lexbor ${CMAKE_CURRENT_BINARY_DIR}/lexbor)
set_target_properties(lexbor_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(pavillion_core PRIVATE src/sanitize.c src/css_filter.c)
target_include_directories(pavillion_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/lexbor/source)
target_compile_definitions(pavillion_core PRIVATE LEXBOR_STATIC)
