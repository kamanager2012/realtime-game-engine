// Fuzz targets for security-critical parsing functions.
// Build with: clang++ -fsanitize=fuzzer,address -std=c++20 -I../..
//
// Target 1: Card::Parse — no crashes/memory errors on arbitrary input
// Target 2: JSON helpers — jsonGetStr/jsonGetNum resilience
// Target 3: Base64 decode — no buffer overflows
// Target 4: JWT payload parsing — nlohmann/json exception safety

#include <cstdint>
#include <string>

// Forward declarations of the functions we fuzz.
// In a real setup these would be includes; simplified for the template.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);

  // --- Harness 1: Card string parsing ---
  // Card::Parse accepts strings like "As", "Kh", "2c", "Td", etc.
  // It should never crash or corrupt memory on arbitrary input.
  // (Import from core/include/poker_engine/evaluator/card.h)
  // try { poker_engine::core::Card::Parse(input); } catch(...) {}

  // --- Harness 2: Base64 decode ---
  // Base64DecodeImpl should handle any input without buffer overflow.
  // (Import from phase13/src/auth_service.cpp)
  // std::string decoded = Base64DecodeImpl(input);

  // --- Harness 3: JWT payload parsing ---
  // nlohmann::json::parse should be exception-safe.
  // try { auto j = nlohmann::json::parse(input); } catch(...) {}

  // --- Harness 4: JSON string/number extraction ---
  // jsonGetStr/jsonGetNum should handle malformed JSON gracefully.
  // jsonGetStr(input, "test");
  // jsonGetNum(input, "test");

  // Placeholder: at minimum ensure no crash on any input.
  (void)input;
  return 0;
}
