#pragma once

#include <flatbuffers/flatbuffers.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "poker_engine/base/logging.h"
#include "poker_engine/base/result.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
// 包含自动生成的 schema
#include "poker_engine/fbs/generated/schema_generated.h"

namespace poker_engine::serialization {

// ==================== FlatBuffers 序列化器 ====================
// 提供 GameState, Action, WSMessage 等的高效二进制序列化

class FlatBufferSerializer {
 public:
  FlatBufferSerializer();
  ~FlatBufferSerializer();

  // ========== GameState 序列化 ==========

  flatbuffers::DetachedBuffer SerializeGameState(const game::GameState& state);

  // 反序列化 — returns TableConfig (GameState requires runtime construction)
  base::Result<game::TableConfig> DeserializeGameState(const void* data, size_t size) const;

  base::Result<game::TableConfig> DeserializeGameState(
      const flatbuffers::DetachedBuffer& buf) const;

  // 安全反序列化（带大小限制）
  base::Result<game::TableConfig> DeserializeGameStateSafe(const void* data, size_t size,
                                                           size_t max_size = 1024 * 1024) const;

  // ========== Action 序列化 ==========

  flatbuffers::DetachedBuffer SerializeAction(const game::GameAction& action);
  base::Result<game::GameAction> DeserializeAction(const void* data, size_t size) const;

  // ========== 批量序列化 ==========

  flatbuffers::DetachedBuffer SerializeActionBatch(const std::vector<game::GameAction>& actions);

  base::Result<std::vector<game::GameAction>> DeserializeActionBatch(const void* data,
                                                                     size_t size) const;

  // ========== WS 消息序列化 ==========

  flatbuffers::DetachedBuffer SerializeWSMessage(fbs::WSMessageType type, uint64_t seq,
                                                 const std::string& payload = "");

  struct DeserializedWSMessage {
    fbs::WSMessageType type;
    uint64_t seq;
    int64_t timestamp_ms;
    std::vector<uint8_t> payload;
  };

  base::Result<DeserializedWSMessage> DeserializeWSMessage(const void* data, size_t size) const;

  // ========== 性能指标序列化 ==========

  flatbuffers::DetachedBuffer SerializeMetrics(int ws_connections, int active_tables,
                                               double messages_per_second, double avg_latency_ms,
                                               double p99_latency_ms);

  // ========== 统计信息 ==========

  struct Stats {
    size_t total_serialized_bytes = 0;
    size_t total_deserialized_bytes = 0;
    size_t serialize_count = 0;
    size_t deserialize_count = 0;
    double avg_serialize_time_us = 0.0;
    double avg_deserialize_time_us = 0.0;
  };

  Stats GetStats() const { return stats_; }
  void ResetStats() { stats_ = {}; }

 private:
  class ArenaAllocator : public flatbuffers::Allocator {
   public:
    ArenaAllocator() = default;
    uint8_t* allocate(size_t size) override;
    void deallocate(uint8_t* p, size_t size) override;

   private:
    std::vector<std::unique_ptr<uint8_t[]>> blocks_;
    std::mutex mutex_;
  };

  ArenaAllocator arena_allocator_;
  mutable Stats stats_;
  mutable std::mutex stats_mutex_;

  // Convert game::ActionType <-> fbs::ActionType
  static fbs::ActionType FlatActionType(game::ActionType type);
  static game::ActionType GameActionType(fbs::ActionType type);
};

}  // namespace poker_engine::serialization
