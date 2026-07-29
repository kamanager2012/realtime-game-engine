#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "poker_engine/concurrency/actor.h"

using namespace poker_engine::concurrency;

class TableSimulatorActor : public Actor {
 public:
  TableSimulatorActor() : Actor(0), pot_(0), hand_count_(0) {
    RegisterHandler("add_chips", [this](const MessageEnvelope& msg) {
      int amount = std::stoi(msg.payload);
      pot_ += amount;
    });
    RegisterHandler("start_hand", [this](const MessageEnvelope&) {
      pot_ = 0;
      hand_count_++;
    });
    RegisterHandler("stress", [this](const MessageEnvelope&) {
      volatile int x = 0;
      for (int i = 0; i < 100; ++i) x += i;
    });
  }
  int Pot() const { return pot_; }
  int HandCount() const { return hand_count_; }

 private:
  int pot_;
  int hand_count_;
};

TEST(ActorStressTest, HighThroughputMessages) {
  TableSimulatorActor actor;
  actor.Start();

  constexpr int kMessages = 100000;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < kMessages; ++i) {
    MessageEnvelope msg;
    msg.sender_id = 0;
    msg.target_actor_id = 0;
    msg.message_type = "stress";
    msg.payload = "";
    actor.Tell(std::move(msg));
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  auto elapsed = std::chrono::high_resolution_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  actor.Stop();

  double msg_per_sec = (kMessages * 1000.0) / ms;

  std::cout << "\nActor throughput: " << kMessages << " msgs in " << ms << "ms (" << msg_per_sec
            << " msg/sec)" << std::endl;

  EXPECT_GT(msg_per_sec, 50000);
}

TEST(ActorStressTest, MultiProducerSingleConsumer) {
  TableSimulatorActor actor;
  actor.Start();

  constexpr int kProducers = 8;
  constexpr int kPerProducer = 10000;
  std::atomic<int> submitted{0};

  auto producer = [&](int id) {
    for (int i = 0; i < kPerProducer; ++i) {
      MessageEnvelope msg;
      msg.sender_id = id;
      msg.target_actor_id = 0;
      msg.message_type = "add_chips";
      msg.payload = "1";
      actor.Tell(std::move(msg));
      submitted++;
    }
  };

  auto start = std::chrono::high_resolution_clock::now();

  std::vector<std::thread> threads;
  for (int i = 0; i < kProducers; ++i) {
    threads.emplace_back(producer, i);
  }
  for (auto& t : threads) t.join();

  std::this_thread::sleep_for(std::chrono::seconds(1));

  auto elapsed = std::chrono::high_resolution_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  actor.Stop();

  EXPECT_EQ(submitted.load(), kProducers * kPerProducer);

  std::cout << "\nMulti-producer: " << submitted.load() << " msgs in " << ms << "ms" << std::endl;
}

TEST(ActorTest, AskTimeoutBehavior) {
  class SlowActor : public Actor {
   public:
    SlowActor() : Actor(1) {
      RegisterHandler("slow", [this](const MessageEnvelope&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      });
    }
  };

  SlowActor actor;
  actor.Start();

  MessageEnvelope msg;
  msg.sender_id = 0;
  msg.target_actor_id = 1;
  msg.message_type = "slow";

  auto start = std::chrono::high_resolution_clock::now();
  actor.Ask(std::move(msg));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::high_resolution_clock::now() - start)
                     .count();

  actor.Stop();
  EXPECT_GE(elapsed, 90);
}
