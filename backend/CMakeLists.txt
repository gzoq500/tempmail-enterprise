cmake_minimum_required(VERSION 3.16)
project(tempmail-server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Download header-only libs if missing
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
)

add_executable(tempmail-server
    src/main.cpp
    src/server.cpp
    src/database.cpp
    src/email_parser.cpp
)

target_link_libraries(tempmail-server
    Threads::Threads
    ${SQLITE3_LIBRARIES}
    ssl crypto
)

target_compile_options(tempmail-server PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
)

install(TARGETS tempmail-server DESTINATION bin)
