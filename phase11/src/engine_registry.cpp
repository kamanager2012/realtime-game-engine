#include "poker_engine/phase11/engine_registry.h"

namespace poker_engine {
namespace phase11 {

EngineRegistry& EngineRegistry::Instance() {
  static EngineRegistry inst;
  return inst;
}

void EngineRegistry::Register(const std::string& name, const std::string& description,
                              EngineFunc func) {
  engines_[name] = {description, func};
}

void EngineRegistry::Unregister(const std::string& name) { engines_.erase(name); }

std::string EngineRegistry::Execute(const std::string& engine_name,
                                    const std::string& input) const {
  auto it = engines_.find(engine_name);
  if (it == engines_.end()) return "Engine not found: " + engine_name;
  return it->second.func(input);
}

std::vector<std::pair<std::string, std::string>> EngineRegistry::ListEngines() const {
  std::vector<std::pair<std::string, std::string>> list;
  for (const auto& [name, info] : engines_) list.push_back({name, info.description});
  return list;
}

bool EngineRegistry::HasEngine(const std::string& name) const { return engines_.count(name) > 0; }

}  // namespace phase11
}  // namespace poker_engine
