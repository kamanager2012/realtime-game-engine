#pragma once
#include <functional>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

#include "poker_engine/game/table.h"
#include "poker_engine/network/ai_engine.h"
#include "poker_engine/network/websocket_server.h"

namespace poker_engine::network {

struct ServerConfig {
  int port = 8080;
  std::string host = "0.0.0.0";
  int max_tables = 10;
  int max_players_per_table = 9;
  double default_small_blind = 0.5;
  double default_big_blind = 1.0;
  double default_min_buy_in = 10;
  double default_max_buy_in = 200;
};

class GameServer {
 public:
  explicit GameServer(const ServerConfig& config = ServerConfig());
  ~GameServer();

  bool Start();
  void Stop();
  bool IsRunning() const;

  // Table management
  std::string CreateTable(const std::string& name, int max_players, double sb, double bb);
  bool CloseTable(const std::string& table_id);
  int TableCount() const { return static_cast<int>(tables_.size()); }
  std::vector<std::string> ListTables() const;

  // Player management
  bool PlayerJoin(int client_id, const std::string& table_id, const std::string& name,
                  double buy_in);
  bool PlayerLeave(int client_id);
  bool PlayerAction(int client_id, const std::string& action, double amount);
  bool PlayerSitDown(int client_id);
  bool PlayerStandUp(int client_id);

  // Game control
  bool StartHand(const std::string& table_id);

  // Lightweight HTTP/WebSocket adapter API used by cli/poker_ws_server.cpp.
  bool JoinTable(int32_t player_id, const std::string& table_id, const std::string& name,
                 int seat_index, int64_t buy_in, const std::string& token);
  bool LeaveTable(int32_t player_id, const std::string& table_id);
  bool StartGame(const std::string& table_id);
  std::string OnPlayerAction(int32_t player_id, const std::string& table_id,
                             const std::string& action, int64_t amount, int64_t request_id);
  int AddBots(const std::string& table_id, int count, int64_t buy_in);
  void ProcessBotActions(const std::string& table_id);
  std::string GetTableStateJSON(const std::string& table_id, int32_t viewer_player_id = 0) const;
  double GetPlayerStack(const std::string& table_id, int32_t player_id) const;
  bool IsPlayerSeated(const std::string& table_id, int32_t player_id) const;
  bool IsSeatAvailable(const std::string& table_id, int seat_index) const;
  bool GetTableBuyInLimits(const std::string& table_id, double& min_buy_in,
                           double& max_buy_in) const;
  std::string GetTablesListJSON() const;
  void SetBroadcastCallback(std::function<void(const std::string&, const std::string&)> cb);
  using TableGameEventCallback =
      std::function<void(const std::string&, poker_engine::game::Table&, const poker_engine::game::GameEvent&)>;
  void SetTableGameEventCallback(TableGameEventCallback cb);


  int GetPort() const { return config_.port; }

 private:
  void HandleMessage(int client_id, const std::string& msg);
  void HandleConnect(int client_id);
  void HandleDisconnect(int client_id);
  void BroadcastTableState(const std::string& table_id);

  ServerConfig config_;
  std::unique_ptr<WebSocketServer> ws_server_;
  std::map<std::string, std::unique_ptr<poker_engine::game::Table>> tables_;
  std::map<int, std::string> client_to_table_;
  std::map<int, int> client_to_player_;
  int next_table_id_ = 1;
  int next_player_id_ = 1;
  int next_bot_id_ = -1;
  std::function<void(const std::string&, const std::string&)> broadcast_callback_;
  TableGameEventCallback table_game_event_callback_;
  std::unordered_map<int32_t, AIEngine> bot_ai_;

  AIEngine& GetOrCreateBotAI(int32_t bot_id);
  static poker_engine::game::GameAction SanitizeBotAction(const poker_engine::game::GameState& state,
                                                          int32_t player_id,
                                                          poker_engine::game::GameAction action,
                                                          double big_blind);
};

}  // namespace poker_engine::network
