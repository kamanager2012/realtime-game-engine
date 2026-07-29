#include "poker_engine/phase4/hh_parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase4 {
namespace fs = std::filesystem;
using namespace poker_engine::evaluator;

// ===================== 工具函数 =====================
std::string HandHistoryParser::Trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::vector<std::string> HandHistoryParser::Split(const std::string& s, char delim) {
  std::vector<std::string> tokens;
  std::istringstream iss(s);
  std::string token;
  while (std::getline(iss, token, delim)) {
    tokens.push_back(token);
  }
  return tokens;
}

// ===================== 字符串 → 枚举 =====================
ActionType HandHistoryParser::ParseActionType(const std::string& s) {
  std::string t = Trim(s);
  std::string lower;
  for (char c : t) lower += std::tolower(c);

  if (lower.find("folds") != std::string::npos) return ActionType::FOLD;
  if (lower.find("checks") != std::string::npos) return ActionType::CHECK;
  if (lower.find("calls") != std::string::npos) return ActionType::CALL;
  if (lower.find("bets") != std::string::npos) return ActionType::BET;
  if (lower.find("raises") != std::string::npos) return ActionType::RAISE;
  if (lower.find("all-in") != std::string::npos || lower.find("all in") != std::string::npos)
    return ActionType::ALL_IN;
  if (lower.find("posts small blind") != std::string::npos ||
      lower.find("posts the small blind") != std::string::npos)
    return ActionType::POST_SB;
  if (lower.find("posts big blind") != std::string::npos ||
      lower.find("posts the big blind") != std::string::npos)
    return ActionType::POST_BB;
  if (lower.find("posts ante") != std::string::npos ||
      lower.find("posts the ante") != std::string::npos)
    return ActionType::POST_ANTE;
  if (lower.find("collected") != std::string::npos) return ActionType::COLLECT;
  if (lower.find("showed") != std::string::npos || lower.find("shows") != std::string::npos) {
    if (lower.find("and wins") != std::string::npos || lower.find("wins") != std::string::npos)
      return ActionType::SHOW_WIN;
    return ActionType::SHOW_TIE;
  }
  if (lower.find("uncalled") != std::string::npos) return ActionType::UNCALLED_BET_RETURN;
  if (lower.find("joins") != std::string::npos) return ActionType::JOIN_TABLE;
  if (lower.find("leaves") != std::string::npos) return ActionType::LEAVE_TABLE;
  return ActionType::CHAT;
}

Street HandHistoryParser::ParseStreet(const std::string& s) {
  std::string lower;
  for (char c : s) lower += std::tolower(c);

  if (lower.find("flop") != std::string::npos) return Street::FLOP;
  if (lower.find("turn") != std::string::npos) return Street::TURN;
  if (lower.find("river") != std::string::npos) return Street::RIVER;
  if (lower.find("show down") != std::string::npos || lower.find("showdown") != std::string::npos)
    return Street::SHOWDOWN;
  return Street::PREFLOP;
}

// ===================== 手牌头解析 =====================
void HandHistoryParser::ParseHeader(std::istringstream& iss, HandHistory& hh) {
  std::string line;
  while (std::getline(iss, line)) {
    line = Trim(line);
    if (line.empty()) continue;

    // Hand ID
    auto pos_id = line.find("Hand #");
    if (pos_id != std::string::npos) {
      std::string num_part = line.substr(pos_id + 6);
      // May have trailing colon: "Hand #123456789:"
      std::string id_str = Trim(Split(num_part, ' ')[0]);
      if (!id_str.empty() && id_str.back() == ':') id_str.pop_back();
      hh.hand_id = std::stoll(id_str);

      // Also extract blinds from "($1/$2)" format
      auto paren = line.find("($");
      if (paren != std::string::npos) {
        std::string blind_part = line.substr(paren + 2);
        auto paren_end = blind_part.find(')');
        if (paren_end != std::string::npos) {
          blind_part = blind_part.substr(0, paren_end);
          // Format: "1/$2" or "0.05/$0.10"
          auto slash = blind_part.find('/');
          if (slash != std::string::npos) {
            std::string sb_str = blind_part.substr(0, slash);
            std::string bb_str = blind_part.substr(slash + 1);
            // Remove $ prefix from bb if present
            if (!bb_str.empty() && bb_str[0] == '$') bb_str = bb_str.substr(1);
            hh.small_blind = std::stod(Trim(sb_str));
            hh.big_blind = std::stod(Trim(bb_str));
          }
        }
      }
    }

    // Table: "Table 'Mars'" — extract name between quotes
    auto pos_table = line.find("Table ");
    if (pos_table != std::string::npos) {
      std::string rest = line.substr(pos_table + 6);
      // Check for quoted name: 'Mars'
      auto q1 = rest.find('\'');
      if (q1 != std::string::npos) {
        auto q2 = rest.find('\'', q1 + 1);
        if (q2 != std::string::npos) {
          hh.table_name = rest.substr(q1 + 1, q2 - q1 - 1);
        } else {
          hh.table_name = Trim(Split(rest, ' ')[0]);
        }
      } else {
        hh.table_name = Trim(Split(rest, ' ')[0]);
      }
    }

    // Tournament
    auto pos_tourney = line.find("Tournament ");
    if (pos_tourney != std::string::npos) {
      std::string rest = line.substr(pos_tourney + 11);
      hh.tournament_id = Trim(Split(rest, ',')[0]);
    }

    // Level
    auto pos_level = line.find("Level:");
    if (pos_level != std::string::npos) {
      std::string level_str = line.substr(pos_level + 6);
      std::istringstream lss(Trim(level_str));
      std::string lnum;
      if (std::getline(lss, lnum, ' ')) {
        hh.tournament_level = std::stoi(Trim(lnum));
      }
    }

    // Blinds — find $ sign to extract amount
    auto pos_sb = line.find("small blind");
    if (pos_sb != std::string::npos) {
      auto dollar = line.find('$', pos_sb);
      if (dollar != std::string::npos) {
        std::string val = line.substr(dollar + 1);
        for (char& c : val)
          if (c == ',') c = ' ';
        hh.small_blind = std::stod(Trim(Split(Trim(val), ' ')[0]));
      }
    }
    auto pos_bb = line.find("big blind");
    if (pos_bb != std::string::npos) {
      auto dollar = line.find('$', pos_bb);
      if (dollar != std::string::npos) {
        std::string val = line.substr(dollar + 1);
        for (char& c : val)
          if (c == ',') c = ' ';
        hh.big_blind = std::stod(Trim(Split(Trim(val), ' ')[0]));
      }
    }
    auto pos_ante = line.find("ante");
    if (pos_ante != std::string::npos) {
      auto dollar = line.find('$', pos_ante);
      if (dollar != std::string::npos) {
        std::string val = line.substr(dollar + 1);
        for (char& c : val)
          if (c == ',') c = ' ';
        hh.ante = std::stod(Trim(Split(Trim(val), ' ')[0]));
      }
    }

    // Seat count: "6-max" — find number before "-max"
    auto pos_max = line.find("-max");
    if (pos_max != std::string::npos && pos_max > 0) {
      // Walk backwards from pos_max to find the number
      size_t num_end = pos_max;
      size_t num_start = pos_max;
      while (num_start > 0 && std::isdigit(line[num_start - 1])) num_start--;
      if (num_start < num_end) {
        hh.max_seats = std::stoi(line.substr(num_start, num_end - num_start));
      }
    }

    // Button
    if (line.find("Seat #") != std::string::npos &&
        line.find("is the button") != std::string::npos) {
      // Format: "Table 'Mars' 6-max Seat #3 is the button"
      auto hash_pos = line.find("Seat #");
      std::string seat_part = line.substr(hash_pos + 6);
      hh.button_seat = std::stoi(Trim(Split(seat_part, ' ')[0]));
    }

    // Seat lines should be handled by ParseSeats — push back
    if (line.find("Seat ") == 0 && line.find("is the button") == std::string::npos) {
      iss.putback('\n');
      for (int i = (int)line.size() - 1; i >= 0; i--) iss.putback(line[i]);
      break;
    }

    // Detect end of header
    if (line.find("*** STARTING HAND") != std::string::npos ||
        line.find("*** HOLE CARDS ***") != std::string::npos ||
        line.find("Dealt") != std::string::npos) {
      // Push this line back for next parser
      iss.putback('\n');
      for (int i = (int)line.size() - 1; i >= 0; i--) iss.putback(line[i]);
      break;
    }

    // Site detection
    if (hh.site.empty()) {
      if (line.find("PokerStars") != std::string::npos)
        hh.site = "PokerStars";
      else if (line.find("GGNetwork") != std::string::npos)
        hh.site = "GGNetwork";
      else if (line.find("888poker") != std::string::npos)
        hh.site = "888poker";
    }

    // Game type
    if (hh.game_type.empty()) {
      if (line.find("No Limit") != std::string::npos)
        hh.game_type = "No Limit Hold'em";
      else if (line.find("Pot Limit") != std::string::npos)
        hh.game_type = "Pot Limit Hold'em";
      else if (line.find("Limit") != std::string::npos &&
               line.find("No Limit") == std::string::npos &&
               line.find("Pot Limit") == std::string::npos)
        hh.game_type = "Limit Hold'em";
    }
  }
}

// ===================== 座位解析 =====================
void HandHistoryParser::ParseSeats(std::istringstream& iss, HandHistory& hh) {
  std::string line;
  while (std::getline(iss, line)) {
    line = Trim(line);
    if (line.empty()) continue;

    if (line.find("Seat ") == 0) {
      HHSeat seat;
      auto tokens = Split(line, ' ');
      if (tokens.size() >= 4) {
        // tokens[1] may be "1:" — strip trailing colon
        std::string seat_num_str = tokens[1];
        if (!seat_num_str.empty() && seat_num_str.back() == ':') seat_num_str.pop_back();
        seat.seat_no = std::stoi(seat_num_str);
        // player name may have trailing colon from format "PlayerA:"
        seat.player_name = Trim(tokens[2]);
        if (!seat.player_name.empty() && seat.player_name.back() == ':')
          seat.player_name.pop_back();
        // stack: "($1000)" or "$1000"
        std::string stack_str = tokens[3];
        for (char& c : stack_str)
          if (c == '$' || c == ',' || c == '(' || c == ')') c = ' ';
        seat.stack = std::stod(Trim(stack_str));

        if (line.find("DEALER") != std::string::npos) {
          hh.button_seat = seat.seat_no;
        }
        if (seat.is_hero) hh.button_seat = seat.seat_no;

        hh.seats.push_back(seat);
        hh.seat_map[seat.player_name] = seat;
      }
    } else {
      // Non-seat line: push back for next parser (ParseActions)
      iss.putback('\n');
      for (int i = (int)line.size() - 1; i >= 0; i--) iss.putback(line[i]);
      break;
    }
  }
}

// ===================== 行动解析 =====================
void HandHistoryParser::ParseActions(std::istringstream& iss, HandHistory& hh) {
  std::string line;
  Street current_street = Street::PREFLOP;

  // Create preflop street
  HHStreet preflop_street;
  preflop_street.street = Street::PREFLOP;
  hh.streets.push_back(preflop_street);
  HHStreet* current_street_data = &hh.streets.back();

  while (std::getline(iss, line)) {
    line = Trim(line);
    if (line.empty()) continue;

    // 街标记检测
    if (line.find("*** FLOP ***") != std::string::npos) {
      // 提取公共牌
      HHStreet sf;
      sf.street = Street::FLOP;
      size_t bracket_start = line.find('[');
      if (bracket_start != std::string::npos) {
        std::string cards_str = line.substr(bracket_start + 1);
        size_t bracket_end = cards_str.find(']');
        if (bracket_end != std::string::npos) {
          cards_str = cards_str.substr(0, bracket_end);
          auto card_tokens = Split(Trim(cards_str), ' ');
          for (const auto& ct : card_tokens) {
            if (ct.size() >= 2) sf.community_cards.push_back(Card::Parse(ct));
          }
        }
      }
      hh.streets.push_back(sf);
      current_street = Street::FLOP;
      current_street_data = &hh.streets.back();
      continue;
    }
    if (line.find("*** TURN ***") != std::string::npos) {
      HHStreet st;
      st.street = Street::TURN;
      size_t bracket = line.find('[');
      if (bracket != std::string::npos) {
        std::string cards_str = line.substr(bracket + 1);
        size_t be = cards_str.find(']');
        if (be != std::string::npos) {
          cards_str = Trim(cards_str.substr(0, be));
          // 取最后一个牌 (转牌)
          auto tokens = Split(cards_str, ' ');
          for (const auto& t : tokens)
            if (t.size() >= 2) st.community_cards.push_back(Card::Parse(t));
        }
      }
      // 包含所有之前的公共牌
      for (auto& ps : hh.streets) {
        for (auto& c : ps.community_cards) st.community_cards.push_back(c);
      }
      hh.streets.push_back(st);
      current_street = Street::TURN;
      current_street_data = &hh.streets.back();
      continue;
    }
    if (line.find("*** RIVER ***") != std::string::npos) {
      HHStreet sr;
      sr.street = Street::RIVER;
      size_t bracket = line.find('[');
      if (bracket != std::string::npos) {
        std::string cards_str = line.substr(bracket + 1);
        size_t be = cards_str.find(']');
        if (be != std::string::npos) {
          cards_str = Trim(cards_str.substr(0, be));
          auto tokens = Split(cards_str, ' ');
          for (const auto& t : tokens)
            if (t.size() >= 2) sr.community_cards.push_back(Card::Parse(t));
        }
      }
      for (auto& ps : hh.streets) {
        for (auto& c : ps.community_cards) sr.community_cards.push_back(c);
      }
      hh.streets.push_back(sr);
      current_street = Street::RIVER;
      current_street_data = &hh.streets.back();
      continue;
    }
    if (line.find("*** SHOW DOWN ***") != std::string::npos) {
      current_street = Street::SHOWDOWN;
      continue;
    }
    if (line.find("*** SUMMARY ***") != std::string::npos) {
      // 进入 summary 解析
      ParseSummary(iss, hh);
      return;
    }

    // 行动解析行
    // 格式: "PlayerName: action $amount" 或 "Dealt to Hero [Ks Ah]"
    if (line.find("Dealt to ") == 0) {
      // 底牌
      size_t bracket = line.find('[');
      if (bracket != std::string::npos) {
        std::string cards_str = line.substr(bracket + 1);
        size_t be = cards_str.find(']');
        if (be != std::string::npos) {
          std::string cards_text = cards_str.substr(0, be);
          auto card_tokens = Split(Trim(cards_text), ' ');
          std::string hole_cards;
          for (const auto& ct : card_tokens) {
            if (ct.size() >= 2) {
              Card c = Card::Parse(ct);
              hh.all_board_cards.push_back(c);
              hole_cards += ct;
            }
          }

          // 找到对应座位并设置底牌
          std::string player_name = line.substr(9, bracket - 10);
          player_name = Trim(player_name);
          if (hh.seat_map.count(player_name)) {
            hh.seat_map[player_name].hole_cards = hole_cards;
          }
        }
      }
      continue;
    }

    // 行动行
    // 解析 "PlayerName: action $X" 或 "PlayerName: action"
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string player_name = Trim(line.substr(0, colon));
      std::string action_part = Trim(line.substr(colon + 1));

      HHAction action;
      action.player_name = player_name;
      action.raw_line = line;
      action.street = current_street;

      // 解析行动类型和金额
      action.action = ParseActionType(action_part);

      // 提取金额
      size_t dollar = action_part.find('$');
      if (dollar != std::string::npos) {
        std::string amount_str = action_part.substr(dollar + 1);
        // 金额可能包含逗号
        for (char& c : amount_str)
          if (c == ',') c = ' ';
        std::istringstream ass(amount_str);
        ass >> action.amount;
      }

      if (current_street_data) {
        current_street_data->actions.push_back(action);
      }
    }
  }
}

// ===================== Summary 解析 =====================
void HandHistoryParser::ParseSummary(std::istringstream& iss, HandHistory& hh) {
  std::string line;
  while (std::getline(iss, line)) {
    line = Trim(line);
    if (line.empty()) continue;
    if (line.find("***") != std::string::npos) {
      if (line.find("SHOW DOWN") != std::string::npos) continue;
      break;  // 下一个 hand 或文件结束
    }

    // 收集底池
    if (line.find("Total pot") != std::string::npos) {
      auto dollar = line.find('$');
      if (dollar != std::string::npos) {
        std::string val = line.substr(dollar + 1);
        for (char& c : val)
          if (c == ',') c = ' ';
        hh.total_pot = std::stod(Trim(Split(Trim(val), ' ')[0]));
      } else {
        // No $ sign, try after "Total pot"
        std::string val = line.substr(9);
        for (char& c : val)
          if (c == '$' || c == ',') c = ' ';
        hh.total_pot = std::stod(Trim(Split(Trim(val), ' ')[0]));
      }
    }

    // 赢家 — may or may not have colon
    if (line.find("collected") != std::string::npos) {
      HHResult result;
      // Find $ amount
      size_t dollar = line.find('$');
      if (dollar != std::string::npos) {
        std::string val = line.substr(dollar + 1);
        for (char& c : val)
          if (c == ',') c = ' ';
        result.amount = std::stod(Trim(Split(Trim(val), ' ')[0]));
      }
      // Extract player name: everything before "collected"
      auto collected_pos = line.find(" collected");
      if (collected_pos != std::string::npos) {
        result.player_name = Trim(line.substr(0, collected_pos));
      } else {
        // Fallback: use colon separator
        auto colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
          result.player_name = Trim(line.substr(0, colon_pos));
        }
      }
      if (line.find("with") != std::string::npos) {
        result.hand_desc = Trim(line.substr(line.find("with") + 4));
      }
      hh.results.push_back(result);
    }

    // Seat 总结
    if (line.find("Seat ") == 0) {
      auto tokens = Split(line, ' ');
      if (tokens.size() >= 3) {
        std::string seat_str = tokens[1];
        if (!seat_str.empty() && seat_str.back() == ':') seat_str.pop_back();
        int seat_no = std::stoi(seat_str);
        std::string name = Trim(tokens[2]);
        if (!name.empty() && name.back() == ':') name.pop_back();

        if (hh.seat_map.count(name)) {
          auto& seat = hh.seat_map[name];
          // 更新最终结果
        }
      }
    }
  }
}

// ===================== 主解析入口 =====================
HandHistory HandHistoryParser::Parse(const std::string& text) {
  std::istringstream iss(text);
  return ParseInternal(iss);
}

HandHistory HandHistoryParser::ParseInternal(std::istringstream& iss) {
  HandHistory hh;

  ParseHeader(iss, hh);
  ParseSeats(iss, hh);
  ParseActions(iss, hh);

  // Sync hole_cards from seat_map to seats vector
  for (auto& seat : hh.seats) {
    if (hh.seat_map.count(seat.player_name)) {
      seat.hole_cards = hh.seat_map[seat.player_name].hole_cards;
    }
  }

  // 标记 Hero
  for (auto& seat : hh.seats) {
    if (!seat.hole_cards.empty()) {
      seat.is_hero = true;
      hh.seat_map[seat.player_name].is_hero = true;
    }
  }

  // 所有公共牌去重排序
  std::vector<Card> seen;
  for (auto& s : hh.streets) {
    for (auto& c : s.community_cards) {
      bool found = false;
      for (auto& sc : seen)
        if (sc.Id() == c.Id()) found = true;
      if (!found) seen.push_back(c);
    }
  }
  hh.all_board_cards = seen;

  total_parsed_++;
  return hh;
}

SourcedHandHistory HandHistoryParser::ParseFromFile(const std::string& filepath) {
  SourcedHandHistory shh;
  shh.source_file = filepath;
  shh.parsed_ok = false;

  std::ifstream f(filepath);
  if (!f.is_open()) {
    shh.error_msg = "Cannot open file: " + filepath;
    failed_parses_++;
    return shh;
  }

  std::stringstream buffer;
  buffer << f.rdbuf();
  f.close();

  shh.hh = Parse(buffer.str());
  shh.parsed_ok = true;
  shh.line_number = 0;
  return shh;
}

std::vector<HandHistory> HandHistoryParser::ParseMultiple(const std::string& text) {
  // 简化: 如果文本有 "Hand #" 则尝试分割多手牌
  std::vector<HandHistory> results;
  size_t pos = 0;

  // 查找所有 "Hand #" 开头
  std::vector<size_t> hand_starts;
  while ((pos = text.find("Hand #", pos)) != std::string::npos) {
    hand_starts.push_back(pos);
    pos++;
  }

  if (hand_starts.size() <= 1) {
    HandHistory hh = Parse(text);
    if (hh.hand_id > 0) results.push_back(hh);
    return results;
  }

  // 分割每手牌
  for (size_t i = 0; i < hand_starts.size(); i++) {
    size_t start = hand_starts[i];
    size_t end = (i + 1 < hand_starts.size()) ? hand_starts[i + 1] : text.size();
    std::string hand_text = text.substr(start, end - start);

    HandHistory hh = Parse(hand_text);
    if (hh.hand_id > 0) results.push_back(hh);
  }

  return results;
}

std::vector<HandHistory> HandHistoryParser::ParseFromDirectory(const std::string& dir_path) {
  std::vector<HandHistory> all_hands;

  if (!fs::exists(dir_path)) {
    std::cerr << "[WARN] Directory not found: " << dir_path << "\n";
    return all_hands;
  }

  for (const auto& entry : fs::directory_iterator(dir_path)) {
    if (entry.path().extension() != ".txt") continue;

    auto result = ParseFromFile(entry.path().string());
    if (result.parsed_ok) {
      all_hands.push_back(result.hh);
    } else {
      std::cerr << "[WARN] Failed to parse: " << entry.path() << " - " << result.error_msg << "\n";
    }
  }
  return all_hands;
}

// ===================== HandHistory 工具方法 =====================
std::string HandHistory::HeroName() const {
  for (const auto& s : seats)
    if (s.is_hero) return s.player_name;
  return "";
}

std::vector<Card> HandHistory::HeroCards() const {
  for (const auto& s : seats)
    if (s.is_hero) {
      if (s.hole_cards.size() >= 4) {
        return {Card::Parse(s.hole_cards.substr(0, 2)), Card::Parse(s.hole_cards.substr(2, 2))};
      }
    }
  return {};
}

int HandHistory::HeroSeatNo() const {
  for (const auto& s : seats)
    if (s.is_hero) return s.seat_no;
  return -1;
}

double HandHistory::HeroStartingStack() const {
  for (const auto& s : seats)
    if (s.is_hero) return s.stack;
  return 0;
}

std::string HandHistory::BoardString() const {
  std::string s;
  for (const auto& c : all_board_cards) s += c.ToString() + " ";
  return s;
}

std::string HandHistory::ToString() const {
  std::ostringstream oss;
  oss << "Hand #" << hand_id;
  if (!site.empty()) oss << " [" << site << "]";
  if (!game_type.empty()) oss << " " << game_type;
  oss << " Table: " << table_name;
  if (tournament_id.empty()) oss << " T#" << tournament_id;
  oss << " Blinds: $" << small_blind << "/$" << big_blind;
  oss << " Max: " << max_seats << "-max";
  oss << "\n";

  for (const auto& seat : seats) {
    oss << "  Seat " << seat.seat_no << ": " << seat.player_name << " ($" << seat.stack << ")"
        << (seat.is_hero ? " [HERO]" : "")
        << (seat.hole_cards.empty() ? "" : " [" + seat.hole_cards + "]") << "\n";
  }

  for (size_t i = 0; i < streets.size(); i++) {
    const auto& st = streets[i];
    const char* names[] = {"PREFLOP", "FLOP", "TURN", "RIVER", "SHOWDOWN"};
    const char* sn = (st.street < Street::NUM_STREETS) ? names[static_cast<int>(st.street)] : "?";

    if (!st.community_cards.empty()) {
      oss << "\n*** " << sn << " *** [";
      for (size_t j = 0; j < st.community_cards.size(); j++) {
        if (j > 0) oss << " ";
        oss << st.community_cards[j].ToString();
      }
      oss << "]\n";
    } else {
      oss << "\n*** " << sn << " ***\n";
    }

    for (const auto& a : st.actions) {
      oss << "  " << a.player_name << ": ";
      switch (a.action) {
        case ActionType::FOLD:
          oss << "folds";
          break;
        case ActionType::CHECK:
          oss << "checks";
          break;
        case ActionType::CALL:
          oss << "calls $" << a.amount;
          break;
        case ActionType::BET:
          oss << "bets $" << a.amount;
          break;
        case ActionType::RAISE:
          oss << "raises to $" << a.amount;
          break;
        case ActionType::ALL_IN:
          oss << "all-in $" << a.amount;
          break;
        case ActionType::POST_SB:
          oss << "posts small blind $" << a.amount;
          break;
        case ActionType::POST_BB:
          oss << "posts big blind $" << a.amount;
          break;
        case ActionType::POST_ANTE:
          oss << "posts ante $" << a.amount;
          break;
        case ActionType::COLLECT:
          oss << "collected $" << a.amount;
          break;
        case ActionType::SHOW_WIN:
          oss << "shows a hand (wins)";
          break;
        case ActionType::SHOW_TIE:
          oss << "shows a hand (ties)";
          break;
        default:
          oss << "(unknown action)";
          break;
      }
      oss << "\n";
    }
  }

  if (!results.empty()) {
    oss << "\n*** SUMMARY ***\n";
    oss << "Total pot: $" << total_pot << "\n";
    for (const auto& r : results) {
      oss << "  " << r.player_name << " collected $" << r.amount;
      if (!r.hand_desc.empty()) oss << " with " << r.hand_desc;
      oss << "\n";
    }
  }

  return oss.str();
}

std::string HandHistory::ToShortSummary() const {
  std::ostringstream oss;
  oss << "Hand #" << hand_id << " | " << site << " | " << game_type;
  oss << " | Hero: " << HeroName();
  auto hc = HeroCards();
  if (hc.size() == 2) oss << " [" << hc[0].ToString() << " " << hc[1].ToString() << "]";
  oss << " | Board: " << BoardString();
  oss << " | Pot: $" << total_pot;
  if (!results.empty()) {
    oss << " | Winner: " << results[0].player_name << " ($" << results[0].amount << ")";
  }
  return oss.str();
}

// ===================== SessionStats =====================
std::string SessionStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "\n=== Session Statistics ===\n\n";
  oss << "Hands:          " << total_hands << "\n";
  oss << "Net Won:        $" << total_net << "\n";
  oss << "BB/100:         " << bb_per_100 << "\n";
  oss << "Total Buy-ins:  $" << total_buyins << "\n";
  oss << "Total Rake:     $" << total_rake << "\n";
  oss << "Agg Factor:     " << agg_factor << "\n";
  oss << "WT SD:          " << wt_sd * 100 << "%\n";
  oss << "\nVPIP by position:\n";
  for (const auto& [pos, count] : vpip_by_pos) {
    oss << "  " << std::setw(8) << std::left << pos << ": " << count << "\n";
  }
  oss << "\nPFR by position:\n";
  for (const auto& [pos, count] : pfr_by_pos) {
    oss << "  " << std::setw(8) << std::left << pos << ": " << count << "\n";
  }
  return oss.str();
}

}  // namespace phase4
}  // namespace poker_engine
