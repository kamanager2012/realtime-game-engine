#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/evaluator/card.h"
#include "poker_engine/evaluator/evaluator.h"

namespace poker_engine {
namespace phase4 {

// ===================== 行动类型 =====================
enum class ActionType : uint8_t {
  FOLD = 0,
  CHECK,
  CALL,
  BET,
  RAISE,
  ALL_IN,
  POST_SB,
  POST_BB,
  POST_ANTE,
  COLLECT,
  SHOW_WIN,
  SHOW_TIE,
  UNCALLED_BET_RETURN,
  JOIN_TABLE,
  LEAVE_TABLE,
  CHAT
};

// ===================== 街道 =====================
enum class Street : uint8_t { PREFLOP = 0, FLOP, TURN, RIVER, SHOWDOWN, NUM_STREETS };

// ===================== 行动 =====================
struct HHAction {
  std::string player_name;
  ActionType action;
  double amount = 0;
  Street street;
  std::string raw_line;
};

// ===================== 座位 =====================
struct HHSeat {
  int seat_no = 0;
  std::string player_name;
  double stack = 0;
  bool is_hero = false;
  std::string hole_cards;
};

// ===================== 街道 =====================
struct HHStreet {
  Street street;
  std::vector<poker_engine::Card> community_cards;
  std::vector<HHAction> actions;
};

// ===================== 结果 =====================
struct HHResult {
  std::string player_name;
  double amount = 0;
  std::string hand_desc;
};

// ===================== 手牌 =====================
struct HandHistory {
  int64_t hand_id = 0;
  std::string site;
  std::string game_type;
  std::string table_name;
  std::string tournament_id;
  int tournament_level = 0;
  double small_blind = 0;
  double big_blind = 0;
  double ante = 0;
  int max_seats = 0;
  int button_seat = -1;

  std::vector<HHSeat> seats;
  std::map<std::string, HHSeat> seat_map;
  std::vector<HHStreet> streets;
  std::vector<HHResult> results;

  double total_pot = 0;
  std::vector<poker_engine::Card> all_board_cards;

  // Hero info
  poker_engine::Card hero_cards_[2];
  bool hero_cards_known_ = false;

  // Methods
  std::string HeroName() const;
  std::vector<poker_engine::Card> HeroCards() const;
  int HeroSeatNo() const;
  double HeroStartingStack() const;
  std::string BoardString() const;
  std::string ToString() const;
  std::string ToShortSummary() const;
};

// ===================== 解析结果 =====================
struct SourcedHandHistory {
  HandHistory hh;
  std::string source_file;
  int line_number = 0;
  bool parsed_ok = false;
  std::string error_msg;
};

// ===================== 解析器 =====================
class HandHistoryParser {
 public:
  HandHistoryParser() : total_parsed_(0), failed_parses_(0) {}

  HandHistory Parse(const std::string& text);
  std::vector<HandHistory> ParseMultiple(const std::string& text);
  SourcedHandHistory ParseFromFile(const std::string& filepath);
  std::vector<HandHistory> ParseFromDirectory(const std::string& dir_path);

  int64_t TotalHandsParsed() const { return total_parsed_; }
  int64_t FailedParses() const { return failed_parses_; }

  // Utility
  static ActionType ParseActionType(const std::string& s);
  static Street ParseStreet(const std::string& s);

 private:
  int64_t total_parsed_;
  int64_t failed_parses_;

  static std::string Trim(const std::string& s);
  static std::vector<std::string> Split(const std::string& s, char delim);

  void ParseHeader(std::istringstream& iss, HandHistory& hh);
  void ParseSeats(std::istringstream& iss, HandHistory& hh);
  void ParseActions(std::istringstream& iss, HandHistory& hh);
  void ParseSummary(std::istringstream& iss, HandHistory& hh);
  HandHistory ParseInternal(std::istringstream& iss);
};

// ===================== 会话统计 =====================
struct SessionStats {
  int total_hands = 0;
  double total_net = 0;
  double total_invested = 0;
  double bb_per_100 = 0;
  int total_buyins = 0;
  double total_rake = 0;

  std::map<std::string, int> vpip_by_pos;
  std::map<std::string, int> pfr_by_pos;
  double agg_factor = 0;
  double wt_sd = 0;

  std::string ToString() const;
};

}  // namespace phase4
}  // namespace poker_engine
