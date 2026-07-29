#include "poker_engine/game/proto/protocol.h"

#include <iomanip>
#include <sstream>

namespace poker_engine::game::proto {

std::string Message::Serialize() const {
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(3) << static_cast<int>(type) << "|" << player_id << "|"
      << std::fixed << std::setprecision(3) << timestamp << "|" << payload;
  return oss.str();
}

Message Message::Deserialize(const std::string& data) {
  Message msg;
  std::istringstream iss(data);
  std::string token;

  if (std::getline(iss, token, '|')) {
    msg.type = static_cast<MsgType>(std::stoi(token));
  }
  if (std::getline(iss, token, '|')) {
    msg.player_id = std::stoi(token);
  }
  if (std::getline(iss, token, '|')) {
    msg.timestamp = std::stod(token);
  }
  std::getline(iss, msg.payload);
  return msg;
}

}  // namespace poker_engine::game::proto
