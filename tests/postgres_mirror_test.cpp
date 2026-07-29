#include <gtest/gtest.h>

#include "poker_engine/phase14/postgres_mirror.h"

using namespace poker_engine::phase14;

TEST(PostgresMirrorTest, EmptyUrlDoesNotConnect) {
  PostgresMirror mirror;
  EXPECT_FALSE(mirror.Connect(""));
  EXPECT_FALSE(mirror.IsConnected());
}

TEST(PostgresMirrorTest, InvalidUrlDoesNotConnect) {
  PostgresMirror mirror;
  EXPECT_FALSE(mirror.Connect("postgresql://invalid-host:5432/nodb"));
  EXPECT_FALSE(mirror.IsConnected());
}

TEST(PostgresMirrorTest, MirrorWalletEventNoopWhenDisconnected) {
  PostgresMirror mirror;
  WalletMirrorEvent ev;
  ev.player_id = 1;
  ev.delta = -100;
  ev.balance_after = 900;
  ev.tx_type = "buy_in";
  ev.reference = "main";
  EXPECT_FALSE(mirror.MirrorWalletEvent(ev));
}

TEST(PostgresMirrorTest, MirrorAccountNoopWhenDisconnected) {
  PostgresMirror mirror;
  AccountData account;
  account.id = 5;
  account.username = "alice";
  account.password_hash = "salt$hash";
  EXPECT_FALSE(mirror.MirrorAccount(account));
}
