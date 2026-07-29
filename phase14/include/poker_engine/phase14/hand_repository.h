#pragma once
#include <map>
#include <string>
#include <vector>

namespace poker_engine::phase14 {

// ========== 手牌记录 ==========
struct HandRecord {
  int64_t hand_id = 0;
  int64_t session_id = 0;
  std::string timestamp;
  std::string table_name;
  int num_players = 0;
  double small_blind = 0;
  double big_blind = 0;
  double ante = 0;
  std::vector<std::string> community_cards;
  int64_t duration_ms = 0;

  // RNG fairness proof (commit-reveal) for this hand's shuffle.
  std::string rng_proof;

  std::string ToString() const;
};

// ========== 玩家手牌结果 ==========
struct PlayerHandResult {
  int64_t hand_id = 0;
  int32_t player_id = 0;
  std::string player_name;
  std::string hole_cards;
  std::string action_summary;
  double amount_won = 0;
  double amount_wagered = 0;
  double net_profit = 0;
  bool won = false;
  std::string best_hand;
  int hand_rank = 0;
  bool is_hero = false;

  std::string ToString() const;
};

// ========== 手牌操作记录 ==========
struct ActionRecord {
  int64_t id = 0;
  int64_t hand_id = 0;
  int32_t player_id = 0;
  int street = 0;
  std::string action_type;
  double amount = 0;
  double pot_after = 0;
  int action_number = 0;
  std::string timestamp;
};

// ========== 手牌仓库 ==========
class HandRepository {
 public:
  explicit HandRepository(class Database& db);

  // 保存一手完整牌局
  int64_t SaveHand(const HandRecord& hand, const std::vector<PlayerHandResult>& results,
                   const std::vector<ActionRecord>& actions);

  // 根据ID获取手牌
  std::pair<HandRecord, std::vector<PlayerHandResult>> GetHand(int64_t hand_id);

  // 获取手牌的所有操作
  std::vector<ActionRecord> GetHandActions(int64_t hand_id);

  // 查询手牌列表
  std::vector<HandRecord> ListHands(int limit = 100, int offset = 0);

  // 按玩家查询手牌
  std::vector<PlayerHandResult> GetPlayerHands(int32_t player_id, int limit = 100);

  // 统计
  int GetHandCount();
  int64_t GetLastHandId();

  // 原始查询
  class Database& GetDatabase() { return db_; }

 private:
  class Database& db_;
};

}  // namespace poker_engine::phase14
