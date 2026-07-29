#pragma once
#include <cstdint>
#include <fstream>

namespace poker_engine {
namespace base {

template <typename T>
bool WriteBinary(std::ofstream& os, const T& value) {
  return os.write(reinterpret_cast<const char*>(&value), sizeof(T)).good();
}
template <typename T>
bool ReadBinary(std::ifstream& is, T& value) {
  return is.read(reinterpret_cast<char*>(&value), sizeof(T)).good();
}
template <typename T>
bool WriteArray(std::ofstream& os, const T* data, size_t count) {
  return os.write(reinterpret_cast<const char*>(data), sizeof(T) * count).good();
}
template <typename T>
bool ReadArray(std::ifstream& is, T* data, size_t count) {
  return is.read(reinterpret_cast<char*>(data), sizeof(T) * count).good();
}

struct FileHeader {
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t payload_size = 0;
  static constexpr uint32_t MAGIC_V1 = 0x504F5245;
  bool Valid() const { return magic == MAGIC_V1; }
};
struct RangeFileHeader : FileHeader {
  uint32_t num_entries = 0;
  uint32_t flags = 0;
};
struct StrategyFileHeader : FileHeader {
  uint32_t num_nodes = 0;
  uint32_t action_count = 0;
};

}  // namespace base
}  // namespace poker_engine
