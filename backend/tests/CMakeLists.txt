include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Unit tests
add_executable(test_pki test_pki.cpp)
target_link_libraries(test_pki PRIVATE chat-lib GTest::gtest_main)
add_test(NAME PkiTests COMMAND test_pki)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE chat-lib GTest::gtest_main)
add_test(NAME ConfigTests COMMAND test_config)

add_executable(test_totp test_totp.cpp)
target_link_libraries(test_totp PRIVATE chat-lib GTest::gtest_main)
add_test(NAME TotpTests COMMAND test_totp)

add_executable(test_password test_password.cpp)
target_link_libraries(test_password PRIVATE chat-lib GTest::gtest_main)
add_test(NAME PasswordTests COMMAND test_password)

add_executable(test_webauthn test_webauthn.cpp)
target_link_libraries(test_webauthn PRIVATE chat-lib GTest::gtest_main)
add_test(NAME WebAuthnTests COMMAND test_webauthn)

add_executable(test_handler_utils test_handler_utils.cpp)
target_link_libraries(test_handler_utils PRIVATE chat-lib GTest::gtest_main)
add_test(NAME HandlerUtilsTests COMMAND test_handler_utils)

# Integration tests (require PostgreSQL)
add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE chat-lib GTest::gtest_main)
add_test(NAME DatabaseTests COMMAND test_database)

# Labels for selective test running
set_tests_properties(PkiTests ConfigTests TotpTests PasswordTests WebAuthnTests HandlerUtilsTests
    PROPERTIES LABELS "unit")
set_tests_properties(DatabaseTests PROPERTIES LABELS "integration")
