#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase4/hh_parser.h"

namespace poker_engine {
namespace phase4 {

// SessionStats is defined in hh_parser.h

class SessionAnalyzer {
 public:
  void AddHand(const HandHistory& hh);
  void LoadDirectory(const std::string& dir_path);
  SessionStats ComputeStats() const;

  std::vector<HandHistory> FilterByPlayer(const std::string& player) const;
  std::vector<HandHistory> FilterByGameType(const std::string& gt) const;
  std::vector<HandHistory> FilterByBBRange(double min_bb, double max_bb) const;

 private:
  std::vector<HandHistory> hands_;
};

}  // namespace phase4
}  // namespace poker_engine
