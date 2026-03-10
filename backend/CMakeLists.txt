cmake_minimum_required(VERSION 3.16)
project(chat-server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Application library (shared between main executable and tests)
set(LIB_SOURCES
    src/auth/pki.cpp
    src/auth/webauthn.cpp
    src/auth/password.cpp
    src/auth/totp.cpp
    src/db/database.cpp
)

add_library(chat-lib STATIC ${LIB_SOURCES})

target_include_directories(chat-lib PUBLIC
    ${UWS_DIR}
    ${USOCKETS_DIR}/src
    ${JSON_DIR}
    src
)

target_link_libraries(chat-lib PUBLIC
    uSockets
    OpenSSL::SSL
    OpenSSL::Crypto
    PkgConfig::LIBPQXX
    argon2
    z
    pthread
)

target_compile_definitions(chat-lib PUBLIC LIBUS_USE_OPENSSL)

# Suppress pqxx deprecation warnings (exec_params, binarystring)
# These APIs are deprecated in pqxx >=7.10 but still required for 7.9.x compatibility
set_source_files_properties(src/db/database.cpp PROPERTIES
    COMPILE_FLAGS "-Wno-deprecated-declarations"
)

# Main executable
add_executable(chat-server src/main.cpp)
target_link_libraries(chat-server PRIVATE chat-lib)

# Testing
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
