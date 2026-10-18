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
    src/activity.c
    platform/posix/posix.c)
target_include_directories(pavillion_core PUBLIC include)
target_include_directories(pavillion_core PRIVATE platform src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(pavillion_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(pavillion_core PUBLIC pthread)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pavillion_core PRIVATE platform/linux/linux.c)
else()
    # No macOS backend until G-6. The core CI job links the fake as its host.
    target_sources(pavillion_core PRIVATE platform/fake/fake.c platform/fake/host.c)
endif()
