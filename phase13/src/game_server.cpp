#include "poker_engine/network/game_server.h"

#include "poker_engine/network/cfr_policy_store.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <cmath>

using poker_engine::game::Chips;

namespace poker_engine::network {

GameServer::GameServer(const ServerConfig& config) : config_(config) {
  ws_server_ = std::make_unique<WebSocketServer>(config.port);

  ws_server_->OnMessage([this](int cid, const std::string& msg) { HandleMessage(cid, msg); });
  ws_server_->OnConnect([this](int cid) { HandleConnect(cid); });
  ws_server_->OnDisconnect([this](int cid) { HandleDisconnect(cid); });
}

GameServer::~GameServer() { Stop(); }

bool GameServer::Start() { return ws_server_->Start(); }

void GameServer::Stop() { ws_server_->Stop(); }

bool GameServer::IsRunning() const { return ws_server_->IsRunning(); }

std::string GameServer::CreateTable(const std::string& name, int max_players, double sb,
                                    double bb) {
  if (static_cast<int>(tables_.size()) >= config_.max_tables) return "";
  if (!name.empty() && tables_.count(name) > 0) return name;

  std::string tid = name.empty() ? "table_" + std::to_string(next_table_id_) : name;
  int32_t numeric_id = next_table_id_++;

  poker_engine::game::TableSettings cfg;
  cfg.name = name;
  cfg.game_type = "NLHE";
  cfg.max_players = max_players;
  cfg.small_blind = sb;
  cfg.big_blind = bb;
  cfg.min_buy_in = config_.default_min_buy_in;
  cfg.max_buy_in = config_.default_max_buy_in;

  auto table = std::make_unique<poker_engine::game::Table>(numeric_id, cfg);
  table->SetCallback(
      [this, tid](const poker_engine::game::TableEvent& event) { BroadcastTableState(tid); });

  if (table_game_event_callback_) {
    table->GetGameStateMut().SetCallback([this, tid, raw = table.get()](
                                             const poker_engine::game::GameEvent& event) {
      if (table_game_event_callback_) {
        table_game_event_callback_(tid, *raw, event);
      }
    });
  }


  tables_[tid] = std::move(table);
  ws_server_->CreateTable(tid, "");

  return tid;
}

bool GameServer::CloseTable(const std::string& table_id) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  tables_.erase(it);
  ws_server_->CloseTable(table_id);
  return true;
}

std::vector<std::string> GameServer::ListTables() const {
  std::vector<std::string> result;
  for (const auto& [id, table] : tables_) {
    result.push_back(id);
  }
  return result;
}

bool GameServer::PlayerJoin(int client_id, const std::string& table_id, const std::string& name,
                            double buy_in) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;

  int pid = next_player_id_++;
  if (!it->second->JoinTable(pid, name, buy_in)) return false;

  client_to_table_[client_id] = table_id;
  client_to_player_[client_id] = pid;

  return true;
}

bool GameServer::PlayerLeave(int client_id) {
  auto tit = client_to_table_.find(client_id);
  if (tit == client_to_table_.end()) return false;

  auto pit = client_to_player_.find(client_id);
  if (pit != client_to_player_.end()) {
    auto table_it = tables_.find(tit->second);
    if (table_it != tables_.end()) {
      table_it->second->LeaveTable(pit->second);
    }
    client_to_player_.erase(pit);
  }
  client_to_table_.erase(tit);
  return true;
}

bool GameServer::PlayerAction(int client_id, const std::string& action, double amount) {
  auto tit = client_to_table_.find(client_id);
  if (tit == client_to_table_.end()) return false;

  auto pit = client_to_player_.find(client_id);
  if (pit == client_to_player_.end()) return false;

  auto table_it = tables_.find(tit->second);
  if (table_it == tables_.end()) return false;

  poker_engine::game::GameAction ga;
  ga.amount = amount;
  if (action == "fold")
    ga.type = poker_engine::game::ActionType::FOLD;
  else if (action == "check")
    ga.type = poker_engine::game::ActionType::CHECK;
  else if (action == "call") {
    ga.type = poker_engine::game::ActionType::CALL;
    ga.amount = amount;
  } else if (action == "bet") {
    ga.type = poker_engine::game::ActionType::BET;
    ga.amount = amount;
  } else if (action == "raise") {
    ga.type = poker_engine::game::ActionType::RAISE;
    ga.amount = amount;
  } else if (action == "allin")
    ga.type = poker_engine::game::ActionType::ALL_IN;
  else
    return false;

  return table_it->second->PlayerAction(pit->second, ga);
}

bool GameServer::PlayerSitDown(int client_id) {
  auto tit = client_to_table_.find(client_id);
  if (tit == client_to_table_.end()) return false;

  auto pit = client_to_player_.find(client_id);
  if (pit == client_to_player_.end()) return false;

  auto table_it = tables_.find(tit->second);
  if (table_it == tables_.end()) return false;

  return table_it->second->SitDown(pit->second);
}

bool GameServer::PlayerStandUp(int client_id) {
  auto tit = client_to_table_.find(client_id);
  if (tit == client_to_table_.end()) return false;

  auto pit = client_to_player_.find(client_id);
  if (pit == client_to_player_.end()) return false;

  auto table_it = tables_.find(tit->second);
  if (table_it == tables_.end()) return false;

  return table_it->second->StandUp(pit->second);
}

bool GameServer::StartHand(const std::string& table_id) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  return it->second->StartHand();
}

namespace {

const char* PhaseNameForClient(poker_engine::game::GamePhase phase) {
  using poker_engine::game::GamePhase;
  switch (phase) {
    case GamePhase::WAITING:
      return "waiting";
    case GamePhase::DEALING:
    case GamePhase::PREFLOP_BETTING:
      return "preflop";
    case GamePhase::FLOP_DEALING:
    case GamePhase::FLOP_BETTING:
      return "flop";
    case GamePhase::TURN_DEALING:
    case GamePhase::TURN_BETTING:
      return "turn";
    case GamePhase::RIVER_DEALING:
    case GamePhase::RIVER_BETTING:
      return "river";
    case GamePhase::SHOWDOWN:
    case GamePhase::PAYOUT:
      return "showdown";
    case GamePhase::HAND_COMPLETE:
      return "handOver";
    default:
      return "waiting";
  }
}

const char* PlayerStatusForClient(poker_engine::game::SeatState state) {
  using poker_engine::game::SeatState;
  switch (state) {
    case SeatState::FOLDED:
      return "folded";
    case SeatState::ALL_IN:
      return "all_in";
    case SeatState::SITTING_OUT:
      return "sitting_out";
    default:
      return "active";
  }
}

std::string JsonEscape(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (std::iscntrl(static_cast<unsigned char>(c))) continue;
    out.push_back(c);
  }
  return out;
}

poker_engine::game::ActionType ActionFromString(const std::string& action) {
  using poker_engine::game::ActionType;
  if (action == "fold") return ActionType::FOLD;
  if (action == "check") return ActionType::CHECK;
  if (action == "call") return ActionType::CALL;
  if (action == "bet") return ActionType::BET;
  if (action == "raise") return ActionType::RAISE;
  if (action == "all_in" || action == "allin") return ActionType::ALL_IN;
  return ActionType::CHECK;
}

}  // namespace

bool GameServer::JoinTable(int32_t player_id, const std::string& table_id, const std::string& name,
                           int seat_index, int64_t buy_in, const std::string&) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  if (IsPlayerSeated(table_id, player_id)) return false;
  if (seat_index < 0) {
    // Auto-assign seat
    return it->second->JoinTable(player_id, name, static_cast<double>(buy_in));
  }
  if (seat_index > 255 || !IsSeatAvailable(table_id, seat_index)) return false;
  return it->second->JoinTable(player_id, name, static_cast<double>(buy_in),
                               static_cast<uint8_t>(seat_index));
}

bool GameServer::LeaveTable(int32_t player_id, const std::string& table_id) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  return it->second->LeaveTable(player_id);
}

bool GameServer::StartGame(const std::string& table_id) { return StartHand(table_id); }

std::string GameServer::OnPlayerAction(int32_t player_id, const std::string& table_id,
                                       const std::string& action, int64_t amount, int64_t) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return "table_not_found";

  poker_engine::game::GameAction ga;
  ga.type = ActionFromString(action);
  ga.amount = static_cast<double>(amount);
  ga.player_id = player_id;
  return it->second->PlayerAction(player_id, ga) ? "ok" : "invalid_action";
}


AIEngine& GameServer::GetOrCreateBotAI(int32_t bot_id) {
  auto it = bot_ai_.find(bot_id);
  if (it != bot_ai_.end()) return it->second;

  AIConfig cfg;
  cfg.name = "Bot" + std::to_string(-bot_id);
  cfg.random_seed = static_cast<int>(bot_id * 7919);
  cfg.difficulty = AIDifficulty::MEDIUM;
  cfg.aggression = 0.58;
  cfg.bluff_frequency = 0.14;
  if (CfrPolicyStore::Instance().IsLoaded()) {
    cfg.strategy = AIStrategyType::CfrModel;
    cfg.model_path = CfrPolicyStore::Instance().ModelPath();
  }
  auto [placed, _] = bot_ai_.emplace(bot_id, AIEngine(cfg));
  return placed->second;
}

poker_engine::game::GameAction GameServer::SanitizeBotAction(
    const poker_engine::game::GameState& state, int32_t player_id,
    poker_engine::game::GameAction action, double big_blind) {
  using poker_engine::game::ActionType;
  using poker_engine::game::GameAction;
  using poker_engine::game::PlayerState;

  const PlayerState* me = nullptr;
  for (const auto& p : state.AllPlayers()) {
    if (p.id == player_id) {
      me = &p;
      break;
    }
  }
  if (!me) {
    action.type = ActionType::FOLD;
    action.amount = 0;
    return action;
  }

  action.player_id = player_id;
  double to_call = state.GetCurrentBet() - me->bet_info.current_bet;
  if (to_call < 0) to_call = 0;
  const double bb = std::max(1.0, big_blind);
  const Chips min_raise_to = state.GetCurrentBet() + bb;

  switch (action.type) {
    case ActionType::CHECK:
      if (to_call > 0) action.type = ActionType::CALL;
      break;
    case ActionType::CALL:
      if (to_call <= 0.001) action.type = ActionType::CHECK;
      break;
    case ActionType::BET:
      if (to_call > 0) action.type = ActionType::RAISE;
      action.amount = std::max(Chips(bb), action.amount);
      action.amount = std::min(action.amount, me->chips);
      if (action.amount <= 0) action.type = ActionType::CHECK;
      break;
    case ActionType::RAISE: {
      action.amount = std::max(min_raise_to, action.amount);
      const Chips needed = action.amount - me->bet_info.current_bet;
      if (needed >= me->chips - 0.001) {
        action.type = ActionType::ALL_IN;
        action.amount = 0;
      } else if (needed <= 0.001) {
        action.type = to_call > 0 ? ActionType::CALL : ActionType::CHECK;
        action.amount = 0;
      }
      break;
    }
    case ActionType::ALL_IN:
      break;
    default:
      break;
  }

  if (action.type == ActionType::FOLD && to_call <= 0.001) {
    action.type = ActionType::CHECK;
  }
  return action;
}

int GameServer::AddBots(const std::string& table_id, int count, int64_t buy_in) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return 0;

  int added = 0;
  for (int i = 0; i < count; ++i) {
    const int bot_id = next_bot_id_--;
    const std::string name = "Bot" + std::to_string(-bot_id);
    if (!it->second->JoinTable(bot_id, name, static_cast<double>(buy_in))) continue;
    {
      AIConfig cfg;
      cfg.name = name;
      cfg.random_seed = static_cast<int>(bot_id * 7919);
      cfg.difficulty = AIDifficulty::MEDIUM;
      cfg.aggression = 0.58;
      cfg.bluff_frequency = 0.14;
      if (CfrPolicyStore::Instance().IsLoaded()) {
        cfg.strategy = AIStrategyType::CfrModel;
        cfg.model_path = CfrPolicyStore::Instance().ModelPath();
      }
      bot_ai_[bot_id] = AIEngine(cfg);
    }
    ++added;
  }
  return added;
}

void GameServer::ProcessBotActions(const std::string& table_id) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return;

  for (int i = 0; i < 30; ++i) {
    const auto& state = it->second->GetGameState();
    int32_t current = state.GetCurrentPlayerId();
    if (current >= 0) break;

    AIEngine& ai = GetOrCreateBotAI(current);
    poker_engine::game::GameAction ga = ai.MakeDecision(state, current);
    ga = SanitizeBotAction(state, current, ga, it->second->GetSettings().big_blind);

    if (!it->second->PlayerAction(current, ga)) {
      double to_call = 0;
      for (const auto& p : state.AllPlayers()) {
        if (p.id == current) {
          to_call = state.GetCurrentBet() - p.bet_info.current_bet;
          break;
        }
      }
      poker_engine::game::GameAction fallback;
      fallback.player_id = current;
      fallback.type = to_call > 0 ? poker_engine::game::ActionType::CALL
                                      : poker_engine::game::ActionType::CHECK;
      if (!it->second->PlayerAction(current, fallback)) break;
    }
  }
}




bool GameServer::IsPlayerSeated(const std::string& table_id, int32_t player_id) const {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  for (const auto& p : it->second->GetGameState().AllPlayers()) {
    if (p.id == player_id && p.seat_state != poker_engine::game::SeatState::EMPTY) return true;
  }
  return false;
}

bool GameServer::IsSeatAvailable(const std::string& table_id, int seat_index) const {
  if (seat_index < 0) return false;
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  for (const auto& p : it->second->GetGameState().AllPlayers()) {
    if (static_cast<int>(p.seat) == seat_index &&
        p.seat_state != poker_engine::game::SeatState::EMPTY) {
      return false;
    }
  }
  return seat_index < static_cast<int>(it->second->GetGameState().AllPlayers().size());
}

bool GameServer::GetTableBuyInLimits(const std::string& table_id, double& min_buy_in,
                                     double& max_buy_in) const {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return false;
  const auto& settings = it->second->GetSettings();
  min_buy_in = settings.min_buy_in;
  max_buy_in = settings.max_buy_in;
  return true;
}

double GameServer::GetPlayerStack(const std::string& table_id, int32_t player_id) const {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return 0;
  for (const auto& p : it->second->GetGameState().AllPlayers()) {
    if (p.id == player_id && p.seat_state != poker_engine::game::SeatState::EMPTY) {
      return p.chips;
    }
  }
  return 0;
}

std::string GameServer::GetTableStateJSON(const std::string& table_id,
                                          int32_t viewer_player_id) const {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return "{}";

  const auto& state = it->second->GetGameState();
  std::ostringstream oss;
  oss << "{\"table_id\":\"" << JsonEscape(table_id) << "\""
      << ",\"hand_number\":0"
      << ",\"phase\":\"" << PhaseNameForClient(state.GetPhase()) << "\""
      << ",\"status\":\"" << (state.IsHandInProgress() ? "playing" : "idle") << "\""
      << ",\"dealer_seat\":0"
      << ",\"pot\":" << static_cast<int64_t>(state.GetPot())
      << ",\"current_bet\":" << static_cast<int64_t>(state.GetCurrentBet()) << ",\"min_raise\":2"
      << ",\"big_blind\":2"
      << ",\"current_player_id\":" << state.GetCurrentPlayerId() << ",\"community_cards\":[";

  const auto& community = state.GetCommunity();
  for (uint8_t i = 0; i < community.count; ++i) {
    if (i) oss << ",";
    oss << static_cast<int>(community.cards[i]);
  }

  oss << "],\"players\":[";
  bool first = true;
  for (const auto& p : state.AllPlayers()) {
    if (p.seat_state == poker_engine::game::SeatState::EMPTY) continue;
    if (!first) oss << ",";
    first = false;
    oss << "{\"player_id\":" << p.id << ",\"seat_index\":" << static_cast<int>(p.seat)
        << ",\"chips\":" << static_cast<int64_t>(p.chips)
        << ",\"bet_this_round\":" << static_cast<int64_t>(p.bet_info.current_bet)
        << ",\"total_invested\":" << static_cast<int64_t>(p.bet_info.total_invested)
        << ",\"hole_cards\":[";
    const bool showdown_visible =
        state.GetPhase() == poker_engine::game::GamePhase::SHOWDOWN ||
        state.GetPhase() == poker_engine::game::GamePhase::PAYOUT ||
        state.GetPhase() == poker_engine::game::GamePhase::HAND_COMPLETE;
    const bool show_hole_cards =
        p.hole_cards.IsDealt() &&
        ((viewer_player_id != 0 && p.id == viewer_player_id) ||
         (showdown_visible && !p.IsFolded()));
    if (show_hole_cards) {
      oss << static_cast<int>(p.hole_cards.card1()) << "," << static_cast<int>(p.hole_cards.card2());
    }
    oss << "],\"status\":\"" << PlayerStatusForClient(p.seat_state) << "\""
        << ",\"action_status\":\"none\""
        << ",\"occupied\":true"
        << ",\"display_name\":\"" << JsonEscape(p.name) << "\"}";
  }
  oss << "],\"side_pots\":[],\"winners\":[]}";
  return oss.str();
}

std::string GameServer::GetTablesListJSON() const {
  std::ostringstream oss;
  oss << "{\"tables\":[";
  bool first = true;
  for (const auto& [id, table] : tables_) {
    if (!first) oss << ",";
    first = false;
    int occupied = 0;
    for (const auto& p : table->GetGameState().AllPlayers()) {
      if (p.seat_state != poker_engine::game::SeatState::EMPTY) ++occupied;
    }
    const auto& s = table->GetSettings();
    oss << "{\"id\":\"" << JsonEscape(id) << "\""
        << ",\"name\":\"" << JsonEscape(s.name) << "\""
        << ",\"game_type\":\"" << JsonEscape(s.game_type) << "\""
        << ",\"sb\":" << s.small_blind << ",\"bb\":" << s.big_blind
        << ",\"ante\":" << s.ante
        << ",\"occupied\":" << occupied << ",\"max\":" << table->GetGameState().AllPlayers().size()
        << ",\"status\":\"" << (table->IsPlaying() ? "playing" : "idle") << "\"}";
  }
  oss << "]}";
  return oss.str();
}


void GameServer::SetTableGameEventCallback(TableGameEventCallback cb) {
  table_game_event_callback_ = std::move(cb);
  for (auto& [tid, table] : tables_) {
    table->GetGameStateMut().SetCallback([this, tid, raw = table.get()](
                                             const poker_engine::game::GameEvent& event) {
      if (table_game_event_callback_) {
        table_game_event_callback_(tid, *raw, event);
      }
    });
  }
}

void GameServer::SetBroadcastCallback(
    std::function<void(const std::string&, const std::string&)> cb) {
  broadcast_callback_ = std::move(cb);
}

void GameServer::HandleMessage(int client_id, const std::string& msg) {
  // Simple JSON-like message handling
  // In production, use a proper JSON parser
  std::cout << "[Server] Message from " << client_id << ": " << msg << "\n";
}

void GameServer::HandleConnect(int client_id) {
  std::cout << "[Server] Client " << client_id << " connected\n";
}

void GameServer::HandleDisconnect(int client_id) {
  PlayerLeave(client_id);
  std::cout << "[Server] Client " << client_id << " disconnected\n";
}

void GameServer::BroadcastTableState(const std::string& table_id) {
  auto it = tables_.find(table_id);
  if (it == tables_.end()) return;

  std::string state = GetTableStateJSON(table_id);
  if (broadcast_callback_) {
    broadcast_callback_(table_id, state);
  }
  ws_server_->BroadcastToTable(table_id, state);
}

}  // namespace poker_engine::network
