#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace poker_engine::game {

// Dealer owns a 52-card deck and produces shuffled deals.
//
// Fairness model (production-grade CSPRNG):
//   - The shuffle seed is a 256-bit value drawn from the OS entropy source
//     (OpenSSL RAND_bytes / getrandom), NEVER std::random_device.
//   - Before each shuffle the Dealer publishes a *commitment*:
//         commitment = SHA256(seed || nonce)
//     where `nonce` is freshly generated random data. The commitment is safe
//     to publish up-front because it leaks nothing about the upcoming deck.
//   - Fisher-Yates shuffle uses CSPRNG output directly for each swap index.
//   - After the hand the Dealer can *reveal* (seed, nonce) so any auditor
//     can recompute SHA256(seed||nonce) == commitment and replay the exact
//     Fisher-Yates sequence, proving the deck was not altered after the deal.
//   - HandProof() returns the full audit record (commitment + revealed seed/
//     nonce + final deck hash) for storage alongside the hand history.
class Dealer {
 public:
  Dealer();

  // (Re)shuffle using a fresh cryptographic seed and publish a new commitment.
  void Shuffle();

  std::vector<uint8_t> Deal(int count);
  uint8_t DealOne();
  void Burn();
  int Remaining() const;
  void Reset();

  void DealFlop(std::vector<uint8_t>& out_flop);
  uint8_t DealTurn();
  uint8_t DealRiver();

  std::string ToString() const;

  // ----- Fairness / audit API -----

  // The commitment published BEFORE the deal is revealed.
  const std::string& Commitment() const { return commitment_; }

  // Full, auditable proof of this hand's shuffle. Safe to persist with the
  // hand record. Contains commitment, revealed seed+nonce, and deck hash.
  struct HandProof {
    std::string commitment;   // SHA256(seed||nonce), published pre-deal
    std::string seed_hex;     // revealed post-deal (hex-encoded 32 bytes)
    std::string nonce;        // revealed post-deal (hex)
    std::string deck_hash;    // SHA256 of final ordered deck (hex)
    std::string ToString() const;
    std::string ToJSON() const;  // structured JSON for programmatic audit
  };
  HandProof GetProof() const;

  // Deterministically reseed for tests / replay verification.
  // Calling Reseed(seed_hex, nonce) then ShuffleWithSeed() reproduces an exact
  // prior shuffle, which is how an auditor re-verifies a stored proof.
  void Reseed(const std::string& seed_hex, const std::string& nonce_hex);
  void ShuffleWithSeed();

  const std::array<uint8_t, 32>& Seed() const { return seed_; }

 private:
  void GenerateSeed();
  void ComputeProof();

  std::vector<uint8_t> deck_;
  std::vector<uint8_t> used_stack_;
  int deck_index_ = 0;
  std::array<uint8_t, 32> seed_{};  // 256-bit cryptographic seed

  std::string nonce_;        // random bytes, hex-encoded
  std::string commitment_;   // SHA256(seed||nonce), published pre-reveal
  std::string deck_hash_;    // SHA256 of ordered deck after shuffle
};

}  // namespace poker_engine::game
