#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/range/range.h"

namespace poker_engine {
namespace range {

class RangeManager {
  std::unordered_map<std::string, Range> ranges_;

 public:
  void SetRange(const std::string& name, const Range& r) { ranges_[name] = r; }
  std::optional<Range> GetRange(const std::string& name) const {
    auto it = ranges_.find(name);
    return it != ranges_.end() ? std::make_optional(it->second) : std::nullopt;
  }
  bool RemoveRange(const std::string& name) { return ranges_.erase(name) > 0; }
  std::vector<std::string> ListRangeNames() const {
    std::vector<std::string> n;
    n.reserve(ranges_.size());
    for (auto& [k, _] : ranges_) n.push_back(k);
    return n;
  }
  int ImportDirectory(const std::string& dir) {
    int c = 0;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
      if (e.path().extension() == ".range") {
        auto r = Range::LoadBinary(e.path().string());
        if (r.NonZeroCount() > 0) {
          ranges_[e.path().stem().string()] = std::move(r);
          c++;
        }
      }
    }
    return c;
  }
  size_t Count() const { return ranges_.size(); }
  bool HasRange(const std::string& n) const { return ranges_.count(n) > 0; }
};

}  // namespace range
}  // namespace poker_engine
