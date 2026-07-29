#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace poker_engine::game::proto {

// ========== 消息类型 ==========
enum class MsgType : uint8_t {
  // Client -> Server
  C2S_JOIN = 1,
  C2S_LEAVE = 2,
  C2S_SIT_DOWN = 3,
  C2S_STAND_UP = 4,
  C2S_ACTION = 5,
  C2S_CHAT = 6,
  C2S_READY = 7,

  // Server -> Client
  S2C_WELCOME = 101,
  S2C_TABLE_STATE = 102,
  S2C_HAND_START = 103,
  S2C_CARDS_DEALT = 104,
  S2C_COMMUNITY_CARDS = 105,
  S2C_ACTION = 106,
  S2C_POT_UPDATE = 107,
  S2C_SHOWDOWN = 108,
  S2C_PAYOUT = 109,
  S2C_HAND_END = 110,
  S2C_CHAT = 111,
  S2C_ERROR = 200,

  // General
  PING = 254,
  PONG = 255
};

// ========== 基础消息 ==========
struct Message {
  MsgType type;
  std::string payload;
  int32_t player_id = 0;
  double timestamp = 0;

  std::string Serialize() const;
  static Message Deserialize(const std::string& data);
};

// ========== 客户端请求 ==========
struct JoinRequest {
  std::string player_name;
  std::string auth_token;
  double buy_in = 0;
};

struct ActionRequest {
  int32_t player_id = 0;
  uint8_t action_type = 0;
  double amount = 0;
};

struct SitDownRequest {
  int32_t player_id = 0;
  uint8_t seat = 0;
};

// ========== 服务器推送 ==========
struct PlayerInfoMsg {
  int32_t id = 0;
  std::string name;
  double chips = 0;
  uint8_t seat = 255;
  std::string state;
  std::vector<uint8_t> hole_cards;
  double current_bet = 0;
  bool is_hero = false;
};

struct TableStateMsg {
  int32_t table_id = 0;
  std::string table_name;
  int num_players = 0;
  double pot = 0;
  double small_blind = 0;
  double big_blind = 0;
  int8_t dealer_seat = -1;
  int8_t sb_seat = -1;
  int8_t bb_seat = -1;
  int8_t action_seat = -1;
  std::vector<uint8_t> community_cards;
  std::string game_phase;
};

}  // namespace poker_engine::game::proto
