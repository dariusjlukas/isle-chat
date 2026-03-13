#include <gtest/gtest.h>
#include "auth/password.h"

// --- Password Validation Tests ---

TEST(PasswordValidation, ValidPassword) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 8;
    policy.require_uppercase = true;
    policy.require_lowercase = true;
    policy.require_number = true;
    policy.require_special = false;

    EXPECT_EQ(password_auth::validate_password("Hello123", policy), "");
    EXPECT_EQ(password_auth::validate_password("MyP4ssword", policy), "");
    EXPECT_EQ(password_auth::validate_password("abcDEF99", policy), "");
}

TEST(PasswordValidation, TooShort) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 8;
    policy.require_uppercase = false;
    policy.require_lowercase = false;
    policy.require_number = false;
    policy.require_special = false;

    std::string err = password_auth::validate_password("short", policy);
    EXPECT_NE(err, "");
    EXPECT_NE(err.find("8"), std::string::npos); // should mention the min length
}

TEST(PasswordValidation, ExactMinLength) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 5;
    policy.require_uppercase = false;
    policy.require_lowercase = false;
    policy.require_number = false;
    policy.require_special = false;

    EXPECT_EQ(password_auth::validate_password("abcde", policy), "");
    EXPECT_NE(password_auth::validate_password("abcd", policy), "");
}

TEST(PasswordValidation, RequireUppercase) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 1;
    policy.require_uppercase = true;
    policy.require_lowercase = false;
    policy.require_number = false;
    policy.require_special = false;

    EXPECT_NE(password_auth::validate_password("alllowercase", policy), "");
    EXPECT_EQ(password_auth::validate_password("hasUppercase", policy), "");
}

TEST(PasswordValidation, RequireLowercase) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 1;
    policy.require_uppercase = false;
    policy.require_lowercase = true;
    policy.require_number = false;
    policy.require_special = false;

    EXPECT_NE(password_auth::validate_password("ALLUPPERCASE", policy), "");
    EXPECT_EQ(password_auth::validate_password("HASLOWERcase", policy), "");
}

TEST(PasswordValidation, RequireNumber) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 1;
    policy.require_uppercase = false;
    policy.require_lowercase = false;
    policy.require_number = true;
    policy.require_special = false;

    EXPECT_NE(password_auth::validate_password("nonumbers", policy), "");
    EXPECT_EQ(password_auth::validate_password("has1number", policy), "");
}

TEST(PasswordValidation, RequireSpecial) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 1;
    policy.require_uppercase = false;
    policy.require_lowercase = false;
    policy.require_number = false;
    policy.require_special = true;

    EXPECT_NE(password_auth::validate_password("nospecials123", policy), "");
    EXPECT_EQ(password_auth::validate_password("has!special", policy), "");
    EXPECT_EQ(password_auth::validate_password("has@special", policy), "");
    EXPECT_EQ(password_auth::validate_password("has#special", policy), "");
    EXPECT_EQ(password_auth::validate_password("space counts", policy), "");
}

TEST(PasswordValidation, AllRequirements) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 10;
    policy.require_uppercase = true;
    policy.require_lowercase = true;
    policy.require_number = true;
    policy.require_special = true;

    // Missing everything
    EXPECT_NE(password_auth::validate_password("a", policy), "");
    // Missing special
    EXPECT_NE(password_auth::validate_password("Abcdefgh1X", policy), "");
    // Has everything
    EXPECT_EQ(password_auth::validate_password("Abcdefgh1!", policy), "");
}

TEST(PasswordValidation, EmptyPassword) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 1;
    policy.require_uppercase = false;
    policy.require_lowercase = false;
    policy.require_number = false;
    policy.require_special = false;

    EXPECT_NE(password_auth::validate_password("", policy), "");
}

TEST(PasswordValidation, NoRequirements) {
    password_auth::PasswordPolicy policy;
    policy.min_length = 0;
    policy.require_uppercase = false;
    policy.require_lowercase = false;
    policy.require_number = false;
    policy.require_special = false;

    // Even empty should pass with no requirements
    EXPECT_EQ(password_auth::validate_password("", policy), "");
    EXPECT_EQ(password_auth::validate_password("anything", policy), "");
}

// --- Password Hashing Tests ---

TEST(PasswordHash, HashProducesNonEmpty) {
    std::string hash = password_auth::hash_password("testpassword");
    EXPECT_FALSE(hash.empty());
}

TEST(PasswordHash, HashStartsWithArgon2id) {
    std::string hash = password_auth::hash_password("testpassword");
    EXPECT_EQ(hash.substr(0, 10), "$argon2id$");
}

TEST(PasswordHash, DifferentPasswordsDifferentHashes) {
    std::string hash1 = password_auth::hash_password("password1");
    std::string hash2 = password_auth::hash_password("password2");
    EXPECT_NE(hash1, hash2);
}

TEST(PasswordHash, SamePasswordDifferentSalts) {
    // Each call generates a new random salt, so hashes should differ
    std::string hash1 = password_auth::hash_password("samepassword");
    std::string hash2 = password_auth::hash_password("samepassword");
    EXPECT_NE(hash1, hash2);
}

// --- Password Verification Tests ---

TEST(PasswordVerify, CorrectPassword) {
    std::string hash = password_auth::hash_password("mypassword");
    EXPECT_TRUE(password_auth::verify_password("mypassword", hash));
}

TEST(PasswordVerify, WrongPassword) {
    std::string hash = password_auth::hash_password("mypassword");
    EXPECT_FALSE(password_auth::verify_password("wrongpassword", hash));
}

TEST(PasswordVerify, EmptyPassword) {
    std::string hash = password_auth::hash_password("");
    EXPECT_TRUE(password_auth::verify_password("", hash));
    EXPECT_FALSE(password_auth::verify_password("notempty", hash));
}

TEST(PasswordVerify, LongPassword) {
    std::string long_pass(1000, 'x');
    std::string hash = password_auth::hash_password(long_pass);
    EXPECT_TRUE(password_auth::verify_password(long_pass, hash));
    EXPECT_FALSE(password_auth::verify_password(long_pass + "y", hash));
}

TEST(PasswordVerify, UnicodePassword) {
    std::string unicode_pass = "pässwörd™€";
    std::string hash = password_auth::hash_password(unicode_pass);
    EXPECT_TRUE(password_auth::verify_password(unicode_pass, hash));
    EXPECT_FALSE(password_auth::verify_password("password", hash));
}

// --- Password History Tests ---

TEST(PasswordHistory, MatchesKnownHash) {
    std::string password = "historicpassword";
    std::string hash = password_auth::hash_password(password);

    std::vector<std::string> history = {hash};
    EXPECT_TRUE(password_auth::matches_history(password, history));
}

TEST(PasswordHistory, NoMatchInHistory) {
    std::string hash = password_auth::hash_password("oldpassword");

    std::vector<std::string> history = {hash};
    EXPECT_FALSE(password_auth::matches_history("newpassword", history));
}

TEST(PasswordHistory, EmptyHistory) {
    EXPECT_FALSE(password_auth::matches_history("anypassword", {}));
}

TEST(PasswordHistory, MultipleHashes) {
    std::string pw1 = "first", pw2 = "second", pw3 = "third";
    std::string h1 = password_auth::hash_password(pw1);
    std::string h2 = password_auth::hash_password(pw2);
    std::string h3 = password_auth::hash_password(pw3);

    std::vector<std::string> history = {h1, h2, h3};

    EXPECT_TRUE(password_auth::matches_history("first", history));
    EXPECT_TRUE(password_auth::matches_history("second", history));
    EXPECT_TRUE(password_auth::matches_history("third", history));
    EXPECT_FALSE(password_auth::matches_history("fourth", history));
}

// --- PasswordPolicy Defaults ---

TEST(PasswordPolicy, DefaultValues) {
    password_auth::PasswordPolicy policy;
    EXPECT_EQ(policy.min_length, 8);
    EXPECT_TRUE(policy.require_uppercase);
    EXPECT_TRUE(policy.require_lowercase);
    EXPECT_TRUE(policy.require_number);
    EXPECT_FALSE(policy.require_special);
    EXPECT_EQ(policy.max_age_days, 0);
    EXPECT_EQ(policy.history_count, 0);
}
