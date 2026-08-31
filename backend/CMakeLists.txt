cmake_minimum_required(VERSION 3.16)
project(tempmail-server VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Ultra-optimized flags ──
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG -ffunction-sections -fdata-sections -flto")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG -ffunction-sections -fdata-sections -flto -fvisibility=hidden")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s -Wl,--gc-sections -Wl,--strip-all -flto -Wl,--as-needed")

# SQLite minimal build - compile from source with OMIT flags
add_library(sqlite3_minimal STATIC
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
target_compile_definitions(sqlite3_minimal PRIVATE
    SQLITE_OMIT_AUTOVACUUM
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_PROGRESS_CALLBACK
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_OMIT_TCL_VARIABLE
    SQLITE_OMIT_UTF16
    SQLITE_OMIT_COMPLETE
    SQLITE_OMIT_EXPLAIN
    SQLITE_OMIT_TRACE
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_THREADSAFE=0
    SQLITE_DQS=0
    NDEBUG
)
target_compile_options(sqlite3_minimal PRIVATE -Os -ffunction-sections -fdata-sections -flto)
target_include_directories(sqlite3_minimal PUBLIC ${CMAKE_SOURCE_DIR}/third_party)

# ML-KEM-768 (Kyber, PQClean reference implementation)
add_library(kyber768 STATIC
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/cbd.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/indcpa.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/kem.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/ntt.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/poly.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/polyvec.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/reduce.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/symmetric-shake.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/verify.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/fips202.c
    ${CMAKE_SOURCE_DIR}/third_party/kyber768/randombytes.c
)
target_include_directories(kyber768 PUBLIC ${CMAKE_SOURCE_DIR}/third_party/kyber768)
target_compile_options(kyber768 PRIVATE -O2 -w)

find_package(Threads REQUIRED)

# cpp-httplib defaults to a listen backlog of 5 — under concurrent floods the
# kernel drops/resets connections before accept(). Raise it.
add_compile_definitions(CPPHTTPLIB_LISTEN_BACKLOG=512)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

add_executable(tempmail-server
    src/main.cpp
    src/server.cpp
    src/database.cpp
    src/email_parser.cpp
    src/crypto.cpp
)

target_link_libraries(tempmail-server
    Threads::Threads
    sqlite3_minimal
    kyber768
    ssl crypto
    dl
)

target_compile_options(tempmail-server PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections -flto>
)

install(TARGETS tempmail-server DESTINATION bin)
