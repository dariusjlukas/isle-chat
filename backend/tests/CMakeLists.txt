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

# Integration tests (require PostgreSQL)
add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE chat-lib GTest::gtest_main)
add_test(NAME DatabaseTests COMMAND test_database)

# Labels for selective test running
set_tests_properties(PkiTests ConfigTests PROPERTIES LABELS "unit")
set_tests_properties(DatabaseTests PROPERTIES LABELS "integration")
