#include <gtest/gtest.h>

#include <thread>

#include "poker_engine/network/auth_middleware.h"
#include "poker_engine/network/auth_service.h"

using namespace poker_engine::network;

class AuthServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auth_ = std::make_unique<AuthService>("test-secret-key-for-testing-only");
  }
  std::unique_ptr<AuthService> auth_;
};

TEST_F(AuthServiceTest, RegistrationCreatesAccount) {
  auto result = auth_->Register("testuser", "password123", "Test User");
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.token.empty());
  EXPECT_GT(result.player_id, 0);
}

TEST_F(AuthServiceTest, RegistrationRejectsShortUsername) {
  auto result = auth_->Register("ab", "password123", "Short");
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("3-32"), std::string::npos);
}

TEST_F(AuthServiceTest, RegistrationRejectsShortPassword) {
  auto result = auth_->Register("validuser", "12345", "User");
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("6 characters"), std::string::npos);
}

TEST_F(AuthServiceTest, RegistrationRejectsDuplicateUsername) {
  auth_->Register("user1", "password123", "User One");
  auto result = auth_->Register("user1", "password456", "User Two");
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("already taken"), std::string::npos);
}

TEST_F(AuthServiceTest, LoginWithCorrectCredentials) {
  auth_->Register("loginuser", "securepass", "Login User");
  auto result = auth_->Login("loginuser", "securepass");
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.token.empty());
}

TEST_F(AuthServiceTest, LoginWithWrongPassword) {
  auth_->Register("user2", "correctpassword", "User Two");
  auto result = auth_->Login("user2", "wrongpassword");
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("Invalid"), std::string::npos);
}

TEST_F(AuthServiceTest, LoginWithNonexistentUser) {
  auto result = auth_->Login("nobody", "anypassword");
  EXPECT_FALSE(result.success);
}

TEST_F(AuthServiceTest, TokenVerificationWorks) {
  auth_->Register("verifyuser", "password123");
  auto login_result = auth_->Login("verifyuser", "password123");

  auto auth_result = auth_->Authenticate(login_result.token);
  EXPECT_TRUE(auth_result.success);
  EXPECT_EQ(auth_result.player_id, login_result.player_id);
}

TEST_F(AuthServiceTest, InvalidTokenRejected) {
  auto result = auth_->Authenticate("invalid.token.here");
  EXPECT_FALSE(result.success);
}

TEST_F(AuthServiceTest, LogoutRevokesToken) {
  auth_->Register("logoutuser", "password123");
  auto login = auth_->Login("logoutuser", "password123");

  auth_->Logout(login.token);

  auto result = auth_->Authenticate(login.token);
  EXPECT_FALSE(result.success);
}

TEST_F(AuthServiceTest, UsernameExistsCheck) {
  auth_->Register("existsuser", "password123");
  EXPECT_TRUE(auth_->UsernameExists("existsuser"));
  EXPECT_FALSE(auth_->UsernameExists("doesnotexist"));
}

TEST_F(AuthServiceTest, GetPlayerReturnsAccount) {
  auth_->Register("getuser", "password", "Display Name");
  auto login = auth_->Login("getuser", "password");

  auto account = auth_->GetPlayer(login.player_id);
  ASSERT_TRUE(account.has_value());
  EXPECT_EQ(account->username, "getuser");
  EXPECT_EQ(account->display_name, "Display Name");
  EXPECT_EQ(account->chips, 1000);
  EXPECT_EQ(account->elo_rating, 1500);
}

// ==================== Token 过期测试 ====================

TEST_F(AuthServiceTest, TokenWithShortExpiryExpires) {
  AuthService short_auth("secret", std::chrono::milliseconds(100));

  short_auth.Register("expuser", "password");
  auto login = short_auth.Login("expuser", "password");

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto result = short_auth.Authenticate(login.token);
  EXPECT_FALSE(result.success);
}

// ==================== AuthMiddleware 测试 ====================

class AuthMiddlewareTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auth_ = std::make_unique<AuthService>("middleware-secret");
    middleware_ = std::make_unique<AuthMiddleware>(*auth_);
  }
  std::unique_ptr<AuthService> auth_;
  std::unique_ptr<AuthMiddleware> middleware_;
};

TEST_F(AuthMiddlewareTest, ValidTokenAuthenticatesConnection) {
  auth_->Register("wsuser", "password");
  auto login = auth_->Login("wsuser", "password");

  std::string query = "token=" + login.token + "&table_id=table1";
  auto ctx = middleware_->AuthenticateConnection(query);

  EXPECT_TRUE(ctx.authenticated);
  EXPECT_EQ(ctx.player_id, login.player_id);
  EXPECT_EQ(ctx.table_id, "table1");
  EXPECT_TRUE(ctx.error_message.empty());
}

TEST_F(AuthMiddlewareTest, MissingTokenFails) {
  auto ctx = middleware_->AuthenticateConnection("table_id=table1");
  EXPECT_FALSE(ctx.authenticated);
  EXPECT_NE(ctx.error_message.find("Missing token"), std::string::npos);
}

TEST_F(AuthMiddlewareTest, InvalidTokenFails) {
  std::string query = "token=totally.fake.token&table_id=table1";
  auto ctx = middleware_->AuthenticateConnection(query);
  EXPECT_FALSE(ctx.authenticated);
}

TEST_F(AuthMiddlewareTest, AuthorizeTableCheck) {
  EXPECT_TRUE(middleware_->AuthorizeTable(1, "table1"));
  EXPECT_FALSE(middleware_->AuthorizeTable(-1, "table1"));
  EXPECT_FALSE(middleware_->AuthorizeTable(1, ""));
}
