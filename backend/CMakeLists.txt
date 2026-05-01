cmake_minimum_required(VERSION 3.16)
project(chat-server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system packages
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)

# uWebSockets (header-only, but needs uSockets built)
set(USOCKETS_DIR ${CMAKE_SOURCE_DIR}/libs/uWebSockets/uSockets)
set(UWS_DIR ${CMAKE_SOURCE_DIR}/libs/uWebSockets/src)

# Build uSockets as a static library
file(GLOB USOCKETS_SRC
    ${USOCKETS_DIR}/src/*.c
    ${USOCKETS_DIR}/src/eventing/*.c
    ${USOCKETS_DIR}/src/crypto/*.c
    ${USOCKETS_DIR}/src/crypto/*.cpp
    ${USOCKETS_DIR}/src/internal/*.c
)
add_library(uSockets STATIC ${USOCKETS_SRC})
target_include_directories(uSockets PUBLIC ${USOCKETS_DIR}/src)
target_link_libraries(uSockets OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(uSockets PUBLIC LIBUS_USE_OPENSSL)

# nlohmann/json (header-only)
set(JSON_DIR ${CMAKE_SOURCE_DIR}/libs/json/single_include)

# cpp-httplib (header-only HTTP client, uses OpenSSL for HTTPS)
set(HTTPLIB_DIR ${CMAKE_SOURCE_DIR}/libs/cpp-httplib)

# Argon2 (built from source)
set(ARGON2_DIR ${CMAKE_SOURCE_DIR}/libs/argon2)
add_library(argon2 STATIC
    ${ARGON2_DIR}/src/argon2.c
    ${ARGON2_DIR}/src/core.c
    ${ARGON2_DIR}/src/encoding.c
    ${ARGON2_DIR}/src/ref.c
    ${ARGON2_DIR}/src/thread.c
    ${ARGON2_DIR}/src/blake2/blake2b.c
)
target_include_directories(argon2 PUBLIC ${ARGON2_DIR}/include)
target_include_directories(argon2 PRIVATE ${ARGON2_DIR}/src ${ARGON2_DIR}/src/blake2)

# spdlog (built as a static library via add_subdirectory)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/spdlog)

# hiredis (Redis pub/sub for horizontal WS scaling). Vendored as a submodule
# so build is self-contained (matches the spdlog/argon2 pattern). Off lets
# tiny single-instance deployments build without a Redis dependency.
option(ENCLAVE_ENABLE_REDIS "Enable Redis pub/sub for horizontal WS scaling" ON)
if(ENCLAVE_ENABLE_REDIS)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
    set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SSL_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENABLE_ASYNC_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/hiredis)
endif()

# Application library (shared between main executable and tests)
set(LIB_SOURCES
    src/auth/pki.cpp
    src/auth/webauthn.cpp
    src/auth/password.cpp
    src/auth/totp.cpp
    src/db/connection_pool.cpp
    src/db/database.cpp
    src/db/db_thread_pool.cpp
    src/handlers/auth_handler.cpp
    src/handlers/auth_payload_utils.cpp
    src/handlers/auth_utils.cpp
    src/handlers/cors_utils.cpp
    src/handlers/admin_handler.cpp
    src/handlers/admin_approval_utils.cpp
    src/handlers/admin_settings_utils.cpp
    src/handlers/channel_handler.cpp
    src/handlers/file_handler.cpp
    src/handlers/file_access_utils.cpp
    src/handlers/format_utils.cpp
    src/handlers/handler_utils.cpp
    src/handlers/notification_handler.cpp
    src/handlers/search_handler.cpp
    src/handlers/user_handler.cpp
    src/handlers/space_handler.cpp
    src/handlers/space_file_handler.cpp
    src/handlers/calendar_handler.cpp
    src/handlers/task_board_handler.cpp
    src/handlers/wiki_handler.cpp
    src/logging/logger.cpp
    src/metrics/metrics.cpp
    src/recurrence.cpp
    src/upload_manager.cpp
    src/ws/ws_handler.cpp
    src/ai/llm_client.cpp
    src/ai/tool_registry.cpp
    src/handlers/ai_handler.cpp
    src/redis/redis_envelope.cpp
    src/redis/redis_publisher.cpp
    src/redis/redis_subscriber.cpp
    src/redis/redis_pubsub.cpp
)

add_library(chat-lib STATIC ${LIB_SOURCES})

target_include_directories(chat-lib PUBLIC
    ${UWS_DIR}
    ${USOCKETS_DIR}/src
    ${JSON_DIR}
    ${HTTPLIB_DIR}
    src
)

target_link_libraries(chat-lib PUBLIC
    uSockets
    OpenSSL::SSL
    OpenSSL::Crypto
    PkgConfig::LIBPQXX
    argon2
    spdlog::spdlog
    z
    pthread
)

target_compile_definitions(chat-lib PUBLIC LIBUS_USE_OPENSSL CPPHTTPLIB_OPENSSL_SUPPORT)

if(ENCLAVE_ENABLE_REDIS)
    target_link_libraries(chat-lib PUBLIC hiredis::hiredis)
    target_compile_definitions(chat-lib PUBLIC BACKEND_HAS_REDIS=1)
endif()

# Compiler warnings for project code
target_compile_options(chat-lib PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Suppress pqxx deprecation warnings (exec_params, binarystring)
# These APIs are deprecated in pqxx >=7.10 but still required for 7.9.x compatibility
set_source_files_properties(src/db/database.cpp PROPERTIES
    COMPILE_FLAGS "-Wno-deprecated-declarations"
)

# Main executable
add_executable(chat-server src/main.cpp)
target_link_libraries(chat-server PRIVATE chat-lib)

# Code coverage
option(CODE_COVERAGE "Enable code coverage instrumentation (gcov)" OFF)
if(CODE_COVERAGE)
    target_compile_options(chat-lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(chat-lib PUBLIC --coverage)
endif()

# Sanitizers (AddressSanitizer + UndefinedBehaviorSanitizer)
option(SANITIZERS "Enable ASan and UBSan for detecting memory errors and undefined behavior" OFF)
if(SANITIZERS)
    set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(chat-lib PUBLIC ${SANITIZER_FLAGS})
    target_link_options(chat-lib PUBLIC ${SANITIZER_FLAGS})
    target_compile_options(uSockets PUBLIC ${SANITIZER_FLAGS})
    target_link_options(uSockets PUBLIC ${SANITIZER_FLAGS})
    target_compile_options(argon2 PUBLIC ${SANITIZER_FLAGS})
    target_link_options(argon2 PUBLIC ${SANITIZER_FLAGS})
    message(STATUS "Sanitizers enabled: AddressSanitizer + UndefinedBehaviorSanitizer")
endif()

# Static analysis
option(STATIC_ANALYSIS "Enable clang-tidy static analysis" OFF)
if(STATIC_ANALYSIS)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-17 clang-tidy-16)
    if(CLANG_TIDY_EXE)
        set_target_properties(chat-lib PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        set_target_properties(chat-server PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "STATIC_ANALYSIS=ON but clang-tidy not found")
    endif()
endif()

# Testing
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
