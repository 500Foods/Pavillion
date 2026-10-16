cmake_minimum_required(VERSION 3.16)
project(pavillion_core C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(pavillion_core STATIC
    src/pavillion.c
    src/dispatch.c
    src/cfg.c
    src/config.c
    src/config_prefs.c
    src/config_fs.c
    src/store.c
    platform/posix/posix.c)
target_include_directories(pavillion_core PUBLIC include)
target_include_directories(pavillion_core PRIVATE platform)
target_compile_options(pavillion_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(pavillion_core PUBLIC pthread)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pavillion_core PRIVATE platform/linux/linux.c)
else()
    # No macOS backend until G-6. The core CI job links the fake as its host.
    target_sources(pavillion_core PRIVATE platform/fake/fake.c platform/fake/host.c)
endif()
