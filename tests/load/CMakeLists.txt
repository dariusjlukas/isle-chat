cmake_minimum_required(VERSION 3.16)
project(loadtest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# nlohmann/json from the backend's vendored copy
set(JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../backend/libs/json/single_include")
if(NOT EXISTS "${JSON_INCLUDE_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR "nlohmann/json not found at ${JSON_INCLUDE_DIR}")
endif()

add_executable(loadtest
  src/main.cpp
  src/config.cpp
  src/stats.cpp
  src/reporter.cpp
  src/http_client.cpp
  src/ws_client.cpp
  src/pki_identity.cpp
  src/virtual_user.cpp
  src/setup.cpp
  src/scenarios/auth_load.cpp
  src/scenarios/messaging.cpp
  src/scenarios/rest_api_mix.cpp
  src/scenarios/file_upload.cpp
  src/scenarios/search.cpp
  src/scenarios/realistic.cpp
)

target_include_directories(loadtest PRIVATE
  ${JSON_INCLUDE_DIR}
  ${CURL_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(loadtest PRIVATE
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread
)

target_compile_options(loadtest PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
