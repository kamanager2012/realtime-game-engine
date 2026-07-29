#include <iostream>
#include <string>

#include "poker_engine/phase11/engine_registry.h"
#include "poker_engine/phase11/solver_manager.h"

using namespace poker_engine::phase11;

void PrintHelp() {
  std::cout << "Poker Engine Unified Tool v1.1\n"
            << "Usage: unified_tool <query>\n\n"
            << "Supported queries:\n"
            << "  'AKs vs 22+'                    Equity (LUT)\n"
            << "  'AKs vs 22+ on QhJd7c'         Equity (MC)\n"
            << "  'preflop BTN'                   Preflop strategy\n"
            << "  'AKs BTN'                       Hand analysis\n"
            << "  'icm 50,30,20 10000,6000,4000'  ICM calc\n"
            << "  'engines'                       List available engines\n"
            << "\nAll natural language queries supported.\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintHelp();
    return 1;
  }

  auto& manager = SolverManager::Instance();
  manager.Initialize();

  std::string query;
  for (int i = 1; i < argc; i++) {
    if (i > 1) query += " ";
    query += argv[i];
  }

  if (query == "engines") {
    std::cout << "=== Available Engines ===\n";
    auto list = EngineRegistry::Instance().ListEngines();
    if (list.empty()) {
      std::cout << "  Built-in: equity, preflop, icm, decision\n";
      std::cout << "  (Registry is empty - engines are used via SolverManager)\n";
    } else {
      for (const auto& [name, desc] : list) std::cout << "  " << name << " - " << desc << "\n";
    }
    return 0;
  }

  auto result = manager.Analyze(query);
  std::cout << result.ToString();

  return 0;
}
