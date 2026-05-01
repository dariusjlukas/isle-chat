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

add_executable(test_auth_utils test_auth_utils.cpp)
target_link_libraries(test_auth_utils PRIVATE chat-lib GTest::gtest_main)
add_test(NAME AuthUtilsTests COMMAND test_auth_utils)

add_executable(test_auth_payload_utils test_auth_payload_utils.cpp)
target_link_libraries(test_auth_payload_utils PRIVATE chat-lib GTest::gtest_main)
add_test(NAME AuthPayloadUtilsTests COMMAND test_auth_payload_utils)

add_executable(test_admin_settings_utils test_admin_settings_utils.cpp)
target_link_libraries(test_admin_settings_utils PRIVATE chat-lib GTest::gtest_main)
add_test(NAME AdminSettingsUtilsTests COMMAND test_admin_settings_utils)

add_executable(test_file_access_utils test_file_access_utils.cpp)
target_link_libraries(test_file_access_utils PRIVATE chat-lib GTest::gtest_main)
add_test(NAME FileAccessUtilsTests COMMAND test_file_access_utils)

add_executable(test_admin_approval_utils test_admin_approval_utils.cpp)
target_link_libraries(test_admin_approval_utils PRIVATE chat-lib GTest::gtest_main)
add_test(NAME AdminApprovalUtilsTests COMMAND test_admin_approval_utils)

add_executable(test_search_handler test_search_handler.cpp)
target_link_libraries(test_search_handler PRIVATE chat-lib GTest::gtest_main)
add_test(NAME SearchHandlerTests COMMAND test_search_handler)

add_executable(test_file_handler test_file_handler.cpp)
target_link_libraries(test_file_handler PRIVATE chat-lib GTest::gtest_main)
add_test(NAME FileHandlerTests COMMAND test_file_handler)

add_executable(test_recurrence test_recurrence.cpp)
target_link_libraries(test_recurrence PRIVATE chat-lib GTest::gtest_main)
add_test(NAME RecurrenceTests COMMAND test_recurrence)

add_executable(test_zip_builder test_zip_builder.cpp)
target_link_libraries(test_zip_builder PRIVATE chat-lib GTest::gtest_main)
add_test(NAME ZipBuilderTests COMMAND test_zip_builder)

add_executable(test_upload_manager test_upload_manager.cpp)
target_link_libraries(test_upload_manager PRIVATE chat-lib GTest::gtest_main)
add_test(NAME UploadManagerTests COMMAND test_upload_manager)

add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE chat-lib GTest::gtest_main)
add_test(NAME LoggerTests COMMAND test_logger)

add_executable(test_metrics test_metrics.cpp)
target_link_libraries(test_metrics PRIVATE chat-lib GTest::gtest_main)
add_test(NAME MetricsTests COMMAND test_metrics)

# Integration tests (require PostgreSQL)
add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE chat-lib GTest::gtest_main)
add_test(NAME DatabaseTests COMMAND test_database)

add_executable(test_space_files test_space_files.cpp)
target_link_libraries(test_space_files PRIVATE chat-lib GTest::gtest_main)
add_test(NAME SpaceFileTests COMMAND test_space_files)

add_executable(test_calendar test_calendar.cpp)
target_link_libraries(test_calendar PRIVATE chat-lib GTest::gtest_main)
add_test(NAME CalendarTests COMMAND test_calendar)

add_executable(test_tasks test_tasks.cpp)
target_link_libraries(test_tasks PRIVATE chat-lib GTest::gtest_main)
add_test(NAME TaskBoardTests COMMAND test_tasks)

add_executable(test_personal_spaces test_personal_spaces.cpp)
target_link_libraries(test_personal_spaces PRIVATE chat-lib GTest::gtest_main)
add_test(NAME PersonalSpacesTests COMMAND test_personal_spaces)

add_executable(test_wiki test_wiki.cpp)
target_link_libraries(test_wiki PRIVATE chat-lib GTest::gtest_main)
add_test(NAME WikiTests COMMAND test_wiki)

add_executable(test_notifications test_notifications.cpp)
target_link_libraries(test_notifications PRIVATE chat-lib GTest::gtest_main)
add_test(NAME NotificationTests COMMAND test_notifications)

add_executable(test_auth_credentials test_auth_credentials.cpp)
target_link_libraries(test_auth_credentials PRIVATE chat-lib GTest::gtest_main)
add_test(NAME AuthCredentialTests COMMAND test_auth_credentials)

# Labels for selective test running
set_tests_properties(PkiTests ConfigTests TotpTests PasswordTests WebAuthnTests HandlerUtilsTests
    AuthUtilsTests AuthPayloadUtilsTests AdminSettingsUtilsTests FileAccessUtilsTests
    AdminApprovalUtilsTests SearchHandlerTests FileHandlerTests RecurrenceTests ZipBuilderTests
    UploadManagerTests LoggerTests MetricsTests
    PROPERTIES LABELS "unit")
# PasswordTests runs many argon2 hash operations; with sanitizers this needs extra time
set_tests_properties(PasswordTests PROPERTIES TIMEOUT 120)
set_tests_properties(DatabaseTests SpaceFileTests CalendarTests TaskBoardTests PersonalSpacesTests
    WikiTests NotificationTests AuthCredentialTests
    PROPERTIES LABELS "integration")
