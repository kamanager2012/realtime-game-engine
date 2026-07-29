// Traced Actor wrapper — adds observability to any Actor
// Uses the actual concurrency::Actor and observability::Tracer APIs

#include <memory>

#include "poker_engine/base/logging.h"
#include "poker_engine/concurrency/actor.h"
#include "poker_engine/observability/tracer.h"

namespace poker_engine::network {

// Creates a traced wrapper around any Actor: before/after each OnReceive,
// a Span is started/ended automatically.
class TracedActor : public concurrency::Actor {
 public:
  TracedActor(std::unique_ptr<Actor> inner, observability::Tracer* tracer)
      : Actor(inner->GetId()), inner_(std::move(inner)), tracer_(tracer) {}

  void Start() override { inner_->Start(); }

  void Stop() override { inner_->Stop(); }

 protected:
  void OnReceive(const concurrency::MessageEnvelope& msg) override {
    observability::SpanContext parent;
    observability::ScopedSpan span(tracer_, msg.message_type, parent);
    span->SetTag("sender_id", std::to_string(msg.sender_id));
    span->SetTag("target_id", std::to_string(msg.target_actor_id));
    span->SetTag("payload_size", std::to_string(msg.payload.size()));

    span->AddEvent("handler_start");
    inner_->Tell(concurrency::MessageEnvelope{msg.sender_id, inner_->GetId(), msg.message_type,
                                              msg.payload, nullptr});
    span->AddEvent("handler_end");
  }

 private:
  std::unique_ptr<Actor> inner_;
  observability::Tracer* tracer_;
};

}  // namespace poker_engine::network
