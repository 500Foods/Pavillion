cmake_minimum_required(VERSION 3.16)
project(pavillion_core C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(pavillion_core STATIC
    src/pavillion.c
    src/cfg.c
    src/config.c
    src/config_prefs.c
    src/config_fs.c
    src/store.c)
target_include_directories(pavillion_core PUBLIC include)
target_compile_options(pavillion_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(pavillion_core PUBLIC pthread)
