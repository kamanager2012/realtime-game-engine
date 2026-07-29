#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace poker_engine {
namespace phase11 {

// ========== 引擎注册中心 ==========
class EngineRegistry {
 public:
  using EngineFunc = std::function<std::string(const std::string& input)>;

  static EngineRegistry& Instance();

  void Register(const std::string& name, const std::string& description, EngineFunc func);

  void Unregister(const std::string& name);

  std::string Execute(const std::string& engine_name, const std::string& input) const;

  std::vector<std::pair<std::string, std::string>> ListEngines() const;

  bool HasEngine(const std::string& name) const;

 private:
  EngineRegistry() = default;

  struct EngineInfo {
    std::string description;
    EngineFunc func;
  };

  std::map<std::string, EngineInfo> engines_;
};

}  // namespace phase11
}  // namespace poker_engine
