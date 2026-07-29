#include "poker_engine/phase14/postgres_mirror.h"

#include <iostream>

namespace poker_engine::phase14 {

#if defined(POKER_HAVE_LIBPQ)
#include <libpq-fe.h>

struct PostgresMirror::Impl {
  PGconn* conn = nullptr;
};

static std::string EscapeLiteral(PGconn* conn, const std::string& value) {
  char* escaped = PQescapeLiteral(conn, value.c_str(), value.size());
  if (!escaped) return "''";
  std::string out(escaped);
  PQfreemem(escaped);
  return out;
}

PostgresMirror::PostgresMirror() : impl_(new Impl()) {}
PostgresMirror::~PostgresMirror() {
  Disconnect();
  delete impl_;
  impl_ = nullptr;
}

bool PostgresMirror::Connect(const std::string& connection_url) {
  Disconnect();
  if (connection_url.empty()) return false;
  impl_->conn = PQconnectdb(connection_url.c_str());
  if (PQstatus(impl_->conn) != CONNECTION_OK) {
    std::cerr << "[PostgresMirror] connect failed: " << PQerrorMessage(impl_->conn) << "\n";
    Disconnect();
    return false;
  }
  enabled_ = true;
  return true;
}

void PostgresMirror::Disconnect() {
  enabled_ = false;
  if (impl_ && impl_->conn) {
    PQfinish(impl_->conn);
    impl_->conn = nullptr;
  }
}

bool PostgresMirror::IsConnected() const {
  return enabled_ && impl_ && impl_->conn && PQstatus(impl_->conn) == CONNECTION_OK;
}


bool PostgresMirror::MirrorAccount(const AccountData& account) {
  if (!IsConnected()) return false;
  const std::string user = EscapeLiteral(impl_->conn, account.username);
  const std::string display = EscapeLiteral(impl_->conn, account.display_name);
  const std::string pass = EscapeLiteral(impl_->conn, account.password_hash);
  const std::string avatar = EscapeLiteral(impl_->conn, account.avatar_url);
  const std::string sql =
      "INSERT INTO accounts (id, username, display_name, password_hash, chips, total_profit, "
      "hands_played, elo_rating, avatar_url) VALUES (" +
      std::to_string(account.id) + ", " + user + ", " + display + ", " + pass + ", " +
      std::to_string(account.chips) + ", " + std::to_string(account.total_profit) + ", " +
      std::to_string(account.hands_played) + ", " + std::to_string(account.elo_rating) + ", " +
      avatar + ") ON CONFLICT (id) DO UPDATE SET username = EXCLUDED.username, display_name = "
      "EXCLUDED.display_name, password_hash = EXCLUDED.password_hash, chips = EXCLUDED.chips, "
      "total_profit = EXCLUDED.total_profit, hands_played = EXCLUDED.hands_played, "
      "elo_rating = EXCLUDED.elo_rating, avatar_url = EXCLUDED.avatar_url";
  PGresult* res = PQexec(impl_->conn, sql.c_str());
  const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
  if (!ok) {
    std::cerr << "[PostgresMirror] MirrorAccount failed: " << PQerrorMessage(impl_->conn) << "\n";
  }
  PQclear(res);
  return ok;
}

bool PostgresMirror::SyncAccountChips(int64_t player_id, int64_t chips) {
  if (!IsConnected()) return false;
  const std::string update_sql =
      "UPDATE accounts SET chips = " + std::to_string(chips) + " WHERE id = " +
      std::to_string(player_id);
  PGresult* update_res = PQexec(impl_->conn, update_sql.c_str());
  const bool updated = PQresultStatus(update_res) == PGRES_COMMAND_OK &&
                       std::string(PQcmdTuples(update_res)) != "0";
  PQclear(update_res);
  if (updated) return true;

  AccountData partial;
  partial.id = player_id;
  partial.username = "mirror_" + std::to_string(player_id);
  partial.display_name = "mirror";
  partial.password_hash = "mirror";
  partial.chips = chips;
  return MirrorAccount(partial);
}

bool PostgresMirror::MirrorWalletEvent(const WalletMirrorEvent& event) {
  if (!IsConnected()) return false;
  if (!SyncAccountChips(event.player_id, event.balance_after)) return false;

  const std::string ref = EscapeLiteral(impl_->conn, event.reference);
  const std::string tx = EscapeLiteral(impl_->conn, event.tx_type);
  const std::string sql =
      "INSERT INTO wallet_transactions (player_id, tx_type, amount, balance_after, reference) "
      "VALUES (" +
      std::to_string(event.player_id) + ", " + tx + ", " + std::to_string(event.delta) + ", " +
      std::to_string(event.balance_after) + ", " + ref + ")";
  PGresult* res = PQexec(impl_->conn, sql.c_str());
  const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
  if (!ok) {
    std::cerr << "[PostgresMirror] MirrorWalletEvent failed: " << PQerrorMessage(impl_->conn)
              << "\n";
  }
  PQclear(res);
  return ok;
}

#else

PostgresMirror::PostgresMirror() = default;
PostgresMirror::~PostgresMirror() = default;

bool PostgresMirror::Connect(const std::string&) {
  enabled_ = false;
  return false;
}

void PostgresMirror::Disconnect() { enabled_ = false; }

bool PostgresMirror::IsConnected() const { return false; }


bool PostgresMirror::MirrorAccount(const AccountData&) { return false; }
bool PostgresMirror::MirrorWalletEvent(const WalletMirrorEvent&) { return false; }

bool PostgresMirror::SyncAccountChips(int64_t, int64_t) { return false; }

#endif

}  // namespace poker_engine::phase14
