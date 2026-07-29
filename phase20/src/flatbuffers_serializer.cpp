#include "poker_engine/serialization/flatbuffers_serializer.h"

#include <iostream>

#include "poker_engine/base/logging.h"
#include "poker_engine/base/result.h"

namespace poker_engine::serialization {
using namespace poker_engine::base;

uint8_t* FlatBufferSerializer::ArenaAllocator::allocate(size_t size) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto block = std::make_unique<uint8_t[]>(size);
  uint8_t* ptr = block.get();
  blocks_.push_back(std::move(block));
  return ptr;
}
void FlatBufferSerializer::ArenaAllocator::deallocate(uint8_t*, size_t) {}

FlatBufferSerializer::FlatBufferSerializer() {}
FlatBufferSerializer::~FlatBufferSerializer() = default;

fbs::ActionType FlatBufferSerializer::FlatActionType(game::ActionType type) {
  switch (type) {
    case game::ActionType::FOLD:
      return fbs::ActionType_Fold;
    case game::ActionType::CHECK:
      return fbs::ActionType_Check;
    case game::ActionType::CALL:
      return fbs::ActionType_Call;
    case game::ActionType::BET:
      return fbs::ActionType_Bet;
    case game::ActionType::RAISE:
      return fbs::ActionType_Raise;
    case game::ActionType::ALL_IN:
      return fbs::ActionType_AllIn;
    default:
      return fbs::ActionType_Fold;
  }
}
game::ActionType FlatBufferSerializer::GameActionType(fbs::ActionType type) {
  switch (type) {
    case fbs::ActionType_Fold:
      return game::ActionType::FOLD;
    case fbs::ActionType_Check:
      return game::ActionType::CHECK;
    case fbs::ActionType_Call:
      return game::ActionType::CALL;
    case fbs::ActionType_Bet:
      return game::ActionType::BET;
    case fbs::ActionType_Raise:
      return game::ActionType::RAISE;
    case fbs::ActionType_AllIn:
      return game::ActionType::ALL_IN;
    default:
      return game::ActionType::FOLD;
  }
}

// Helper: map game::SeatState to fbs::PlayerStatus
static fbs::PlayerStatus FlatPlayerStatus(game::SeatState ss) {
  switch (ss) {
    case game::SeatState::SITTING:
      return fbs::PlayerStatus_Idle;
    case game::SeatState::PLAYING:
      return fbs::PlayerStatus_Active;
    case game::SeatState::FOLDED:
      return fbs::PlayerStatus_Folded;
    case game::SeatState::ALL_IN:
      return fbs::PlayerStatus_AllIn;
    case game::SeatState::SITTING_OUT:
      return fbs::PlayerStatus_SittingOut;
    default:
      return fbs::PlayerStatus_Idle;
  }
}

// Helper: map game::GamePhase to fbs::GamePhase
static fbs::GamePhase FlatGamePhase(game::GamePhase p) {
  switch (p) {
    case game::GamePhase::WAITING:
      return fbs::GamePhase_Waiting;
    case game::GamePhase::DEALING:
      return fbs::GamePhase_Preflop;
    case game::GamePhase::PREFLOP_BETTING:
      return fbs::GamePhase_Preflop;
    case game::GamePhase::FLOP_DEALING:
      return fbs::GamePhase_Flop;
    case game::GamePhase::FLOP_BETTING:
      return fbs::GamePhase_Flop;
    case game::GamePhase::TURN_DEALING:
      return fbs::GamePhase_Turn;
    case game::GamePhase::TURN_BETTING:
      return fbs::GamePhase_Turn;
    case game::GamePhase::RIVER_DEALING:
      return fbs::GamePhase_River;
    case game::GamePhase::RIVER_BETTING:
      return fbs::GamePhase_River;
    case game::GamePhase::SHOWDOWN:
      return fbs::GamePhase_Showdown;
    case game::GamePhase::PAYOUT:
      return fbs::GamePhase_HandOver;
    case game::GamePhase::HAND_COMPLETE:
      return fbs::GamePhase_HandOver;
    default:
      return fbs::GamePhase_Waiting;
  }
}

flatbuffers::DetachedBuffer FlatBufferSerializer::SerializeGameState(const game::GameState& state) {
  flatbuffers::FlatBufferBuilder builder(4096, &arena_allocator_);

  // Community cards
  const auto& comm = state.GetCommunity();
  std::vector<flatbuffers::Offset<fbs::Card>> fb_comm;
  for (uint8_t i = 0; i < comm.count; ++i)
    fb_comm.push_back(fbs::CreateCard(builder, comm.cards[i]));
  auto comm_vec = builder.CreateVector(fb_comm);

  // Players
  const auto& players = state.AllPlayers();
  std::vector<flatbuffers::Offset<fbs::PlayerState>> fb_players;
  for (const auto& p : players) {
    if (p.seat_state == game::SeatState::EMPTY) continue;

    std::vector<flatbuffers::Offset<fbs::Card>> fb_hole;
    if (p.hole_cards.IsDealt()) {
      auto hole_vec_cards = p.hole_cards.ToVector();
      for (auto c : hole_vec_cards) {
        fb_hole.push_back(fbs::CreateCard(builder, c));
      }
    }
    auto hole_vec = builder.CreateVector(fb_hole);
    auto name = builder.CreateString(p.name);

    fb_players.push_back(fbs::CreatePlayerState(
        builder, static_cast<int64_t>(p.id), static_cast<int8_t>(p.seat),
        static_cast<int64_t>(p.chips), static_cast<int64_t>(p.bet_info.current_bet),
        static_cast<int64_t>(p.bet_info.total_invested), FlatPlayerStatus(p.seat_state),
        FlatActionType(p.acted_this_round ? game::ActionType::CHECK : game::ActionType::FOLD), true,
        name, hole_vec, false));
  }
  auto players_vec = builder.CreateVector(fb_players);

  auto state_name = builder.CreateString("table");

  builder.Finish(fbs::CreateGameState(
      builder, state_name, 0, FlatGamePhase(state.GetPhase()), fbs::GameStatus_Playing, 0, 0,
      static_cast<int64_t>(state.GetCurrentBet()), 0, static_cast<int64_t>(state.GetPot()), 0, 0,
      comm_vec, 0, players_vec, 0, 0));
  return builder.Release();
}

base::Result<game::TableConfig> FlatBufferSerializer::DeserializeGameState(const void* data,
                                                                           size_t size) const {
  return DeserializeGameStateSafe(data, size);
}

base::Result<game::TableConfig> FlatBufferSerializer::DeserializeGameState(
    const flatbuffers::DetachedBuffer& buf) const {
  return DeserializeGameStateSafe(buf.data(), buf.size());
}

base::Result<game::TableConfig> FlatBufferSerializer::DeserializeGameStateSafe(const void*, size_t,
                                                                               size_t) const {
  // Full deserialization requires reconstructing GameState with runtime deps
  return Result<game::TableConfig>::Err(MakeErrorCode(Error::OperationNotSupported));
}

flatbuffers::DetachedBuffer FlatBufferSerializer::SerializeAction(const game::GameAction& a) {
  flatbuffers::FlatBufferBuilder builder(64);
  builder.Finish(fbs::CreateAction(builder, FlatActionType(a.type), static_cast<int64_t>(a.amount),
                                   static_cast<uint8_t>(a.street),
                                   static_cast<int64_t>(a.player_id), 0));
  return builder.Release();
}

base::Result<game::GameAction> FlatBufferSerializer::DeserializeAction(const void*, size_t) const {
  return Result<game::GameAction>::Err(MakeErrorCode(Error::OperationNotSupported));
}

flatbuffers::DetachedBuffer FlatBufferSerializer::SerializeActionBatch(
    const std::vector<game::GameAction>& actions) {
  flatbuffers::FlatBufferBuilder builder(actions.size() * 32);
  std::vector<flatbuffers::Offset<fbs::Action>> fb;
  for (const auto& a : actions)
    fb.push_back(fbs::CreateAction(builder, FlatActionType(a.type), static_cast<int64_t>(a.amount),
                                   static_cast<uint8_t>(a.street),
                                   static_cast<int64_t>(a.player_id), 0));
  builder.Finish(builder.CreateVector(fb));
  return builder.Release();
}

base::Result<std::vector<game::GameAction>> FlatBufferSerializer::DeserializeActionBatch(
    const void*, size_t) const {
  return Result<std::vector<game::GameAction>>::Err(MakeErrorCode(Error::OperationNotSupported));
}

flatbuffers::DetachedBuffer FlatBufferSerializer::SerializeWSMessage(fbs::WSMessageType type,
                                                                     uint64_t seq,
                                                                     const std::string& payload) {
  flatbuffers::FlatBufferBuilder builder(256 + payload.size());
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  std::vector<uint8_t> pb(payload.begin(), payload.end());
  builder.Finish(fbs::CreateWSMessage(builder, type, seq, now, builder.CreateVector(pb)));
  return builder.Release();
}

base::Result<FlatBufferSerializer::DeserializedWSMessage>
FlatBufferSerializer::DeserializeWSMessage(const void*, size_t) const {
  return Result<DeserializedWSMessage>::Err(MakeErrorCode(Error::OperationNotSupported));
}

flatbuffers::DetachedBuffer FlatBufferSerializer::SerializeMetrics(int ws, int tables, double mps,
                                                                   double avg_lat, double p99_lat) {
  flatbuffers::FlatBufferBuilder builder(128);
  builder.Finish(fbs::CreatePerformanceMetrics(builder, ws, tables, mps, avg_lat, p99_lat));
  return builder.Release();
}

}  // namespace poker_engine::serialization
