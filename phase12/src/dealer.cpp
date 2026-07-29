#include "poker_engine/game/dealer.h"

#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <openssl/rand.h>
#include <sstream>

namespace poker_engine::game {

namespace {

// Self-contained SHA-256 (no external dependency on the engine core).
// Standard FIPS 180-4 implementation.
class Sha256 {
 public:
  Sha256() { Reset(); }

  void Reset() {
    h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    len_ = 0;
    buf_len_ = 0;
  }

  void Update(const uint8_t* data, size_t size) {
    len_ += size;
    for (size_t i = 0; i < size; ++i) {
      buf_[buf_len_++] = data[i];
      if (buf_len_ == 64) {
        Process(buf_);
        buf_len_ = 0;
      }
    }
  }

  void Update(const std::string& s) {
    Update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  }

  std::string HexDigest() {
    uint8_t raw[32];
    Finalize(raw);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) oss << std::setw(2) << static_cast<int>(raw[i]);
    return oss.str();
  }

  void Finalize(uint8_t out[32]) {
    uint64_t bit_len = len_ * 8;
    uint8_t pad = 0x80;
    Update(&pad, 1);
    uint8_t zero = 0x00;
    while (buf_len_ != 56) Update(&zero, 1);
    uint8_t len_bytes[8];
    for (int i = 0; i < 8; ++i) len_bytes[i] = static_cast<uint8_t>(bit_len >> (56 - 8 * i));
    Update(len_bytes, 8);

    for (int i = 0; i < 8; ++i) {
      out[4 * i] = static_cast<uint8_t>(h_[i] >> 24);
      out[4 * i + 1] = static_cast<uint8_t>(h_[i] >> 16);
      out[4 * i + 2] = static_cast<uint8_t>(h_[i] >> 8);
      out[4 * i + 3] = static_cast<uint8_t>(h_[i]);
    }
  }

 private:
  static uint32_t Ror(uint32_t v, int s) { return (v >> s) | (v << (32 - s)); }
  static uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
  static uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
  static uint32_t Sigma0(uint32_t x) { return Ror(x, 2) ^ Ror(x, 13) ^ Ror(x, 22); }
  static uint32_t Sigma1(uint32_t x) { return Ror(x, 6) ^ Ror(x, 11) ^ Ror(x, 25); }
  static uint32_t sigma0(uint32_t x) { return Ror(x, 7) ^ Ror(x, 18) ^ (x >> 3); }
  static uint32_t sigma1(uint32_t x) { return Ror(x, 17) ^ Ror(x, 19) ^ (x >> 10); }

  void Process(const uint8_t block[64]) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(block[4 * i]) << 24) | (uint32_t(block[4 * i + 1]) << 16) |
             (uint32_t(block[4 * i + 2]) << 8) | uint32_t(block[4 * i + 3]);
    for (int i = 16; i < 64; ++i)
      w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t t1 = hh + Sigma1(e) + Ch(e, f, g) + K[i] + w[i];
      uint32_t t2 = Sigma0(a) + Maj(a, b, c);
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
  }

  std::array<uint32_t, 8> h_;
  uint64_t len_ = 0;
  uint8_t buf_[64];
  size_t buf_len_ = 0;
};

std::string ToHex(const std::string& raw) {
  static const char* k = "0123456789abcdef";
  std::string out;
  out.reserve(raw.size() * 2);
  for (unsigned char c : raw) {
    out.push_back(k[c >> 4]);
    out.push_back(k[c & 0xf]);
  }
  return out;
}

// CSPRNG: draw 256-bit seed from OS entropy (OpenSSL RAND_bytes).
// This replaces the insecure std::random_device + std::mt19937 pattern.
void GenerateSeedImpl(std::array<uint8_t, 32>& seed) {
  if (RAND_bytes(seed.data(), 32) != 1) {
    // Fallback: should never happen, but refuse to operate with bad entropy
    throw std::runtime_error("Dealer: RAND_bytes failed - no entropy available");
  }
}

std::string RandomNonceHex(size_t bytes = 32) {
  std::vector<uint8_t> raw(bytes);
  if (RAND_bytes(raw.data(), static_cast<int>(bytes)) != 1) {
    throw std::runtime_error("Dealer: RAND_bytes failed for nonce");
  }
  return ToHex(std::string(reinterpret_cast<char*>(raw.data()), bytes));
}

// HMAC-SHA256 built on the self-contained Sha256, used as a PRF to expand the
// 256-bit seed into a deterministic, CSPRNG-quality byte stream for the
// Fisher-Yates shuffle. This replaces the previous std::mt19937, which only
// carried 64 bits of entropy and is not a cryptographic RNG.
class HmacSha256 {
 public:
  HmacSha256(const uint8_t* key, size_t key_len) {
    static constexpr size_t kBlock = 64;
    uint8_t k[kBlock] = {0};
    if (key_len > kBlock) {
      Sha256 h;
      h.Update(key, key_len);
      uint8_t tmp[32];
      h.Finalize(tmp);
      memcpy(k, tmp, 32);
    } else {
      memcpy(k, key, key_len);
    }
    uint8_t ipad[kBlock], opad[kBlock];
    for (size_t i = 0; i < kBlock; ++i) {
      ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
      opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    inner_.Update(ipad, kBlock);
    memcpy(opad_, opad, kBlock);
  }

  HmacSha256& Update(const uint8_t* data, size_t size) {
    inner_.Update(data, size);
    return *this;
  }
  HmacSha256& Update(const std::string& s) {
    inner_.Update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return *this;
  }

  void Finalize(uint8_t out[32]) {
    uint8_t inner_hash[32];
    inner_.Finalize(inner_hash);
    Sha256 outer;
    outer.Update(opad_, 64);
    outer.Update(inner_hash, 32);
    outer.Finalize(out);
  }

 private:
  Sha256 inner_;
  uint8_t opad_[64];
};

// Deterministic PRF stream: HMAC-SHA256(seed, "fy" || nonce || block_index).
// Keyed by the full 256-bit seed, so the shuffle's effective entropy is 2^256.
class PrfStream {
 public:
  PrfStream(const std::array<uint8_t, 32>& seed, const std::string& nonce)
      : seed_(seed), nonce_(nonce) {}

  uint8_t NextByte() {
    if (pos_ == 32) {
      HmacSha256 hmac(seed_.data(), seed_.size());
      hmac.Update("fy");
      hmac.Update(nonce_);
      uint8_t ctr[8] = {0};
      for (int i = 0; i < 8; ++i) ctr[i] = static_cast<uint8_t>(block_ >> (56 - 8 * i));
      hmac.Update(ctr, 8);
      uint8_t raw[32];
      hmac.Finalize(raw);
      memcpy(buf_, raw, 32);
      ++block_;
      pos_ = 0;
    }
    return buf_[pos_++];
  }

  // Uniform integer in [0, n-1] via bias-free rejection sampling.
  int Uniform(int n) {
    if (n <= 1) return 0;
    const uint32_t mod = static_cast<uint32_t>(n);
    const uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % mod);
    while (true) {
      uint32_t r = (static_cast<uint32_t>(NextByte()) << 24) |
                   (static_cast<uint32_t>(NextByte()) << 16) |
                   (static_cast<uint32_t>(NextByte()) << 8) |
                   static_cast<uint32_t>(NextByte());
      if (r <= limit) return static_cast<int>(r % mod);
    }
  }

 private:
  std::array<uint8_t, 32> seed_;
  std::string nonce_;
  uint64_t block_ = 0;
  uint8_t buf_[32];
  size_t pos_ = 32;  // force refill on first use
};

}  // namespace

Dealer::Dealer() : deck_(52), deck_index_(0) {
  seed_.fill(0);
  GenerateSeed();
  Reset();
}

void Dealer::GenerateSeed() { GenerateSeedImpl(seed_); }

void Dealer::Shuffle() {
  // Fresh 256-bit seed EVERY hand. Reusing the seed across hands is fatal to
  // provable fairness: once hand #1's (seed, nonce) is revealed, the seed is
  // public and the house could grind nonces offline to choose future decks.
  GenerateSeedImpl(seed_);
  nonce_ = RandomNonceHex(32);
  // Commitment published before the deal: commitment = SHA256(seed || nonce).
  // Leaks nothing about the upcoming deck until the reveal.
  {
    Sha256 h;
    h.Update(seed_.data(), seed_.size());
    h.Update(nonce_);
    commitment_ = h.HexDigest();
  }

  // Expand the full 256-bit seed into a deterministic, CSPRNG-quality stream
  // (HMAC-SHA256 PRF) and drive Fisher-Yates with bias-free rejection sampling.
  // The stream is keyed by the entire seed, so the shuffle's effective entropy
  // is 2^256, and audit replay can reproduce the exact deck from (seed, nonce).
  PrfStream stream(seed_, nonce_);
  for (int i = 51; i > 0; --i) {
    int j = stream.Uniform(i + 1);
    std::swap(deck_[i], deck_[j]);
  }
  deck_index_ = 0;
  ComputeProof();
}

void Dealer::ComputeProof() {
  Sha256 h;
  for (int i = 0; i < 52; ++i) h.Update(&deck_[i], 1);
  deck_hash_ = h.HexDigest();
}

void Dealer::Reseed(const std::string& seed_hex, const std::string& nonce_hex) {
  // Parse hex seed string into the 32-byte array.
  for (size_t i = 0; i < 32 && i * 2 + 1 < seed_hex.size(); ++i) {
    seed_[i] = static_cast<uint8_t>(std::stoul(seed_hex.substr(i * 2, 2), nullptr, 16));
  }
  nonce_ = nonce_hex;
  {
    Sha256 h;
    h.Update(seed_.data(), seed_.size());
    h.Update(nonce_);
    commitment_ = h.HexDigest();
  }
}

void Dealer::ShuffleWithSeed() {
  // Deterministic Fisher-Yates for audit replay — identical to Shuffle().
  // Uses the same HMAC-SHA256 PRF keyed by (seed, nonce), so a stored proof's
  // (seed, nonce) reproduces the exact deck an auditor recomputes.
  PrfStream stream(seed_, nonce_);
  for (int i = 51; i > 0; --i) {
    int j = stream.Uniform(i + 1);
    std::swap(deck_[i], deck_[j]);
  }
  deck_index_ = 0;
  ComputeProof();
}

std::vector<uint8_t> Dealer::Deal(int count) {
  std::vector<uint8_t> dealt;
  dealt.reserve(count);
  for (int i = 0; i < count && deck_index_ < 52; ++i) {
    uint8_t card = deck_[deck_index_++];
    used_stack_.push_back(card);
    dealt.push_back(card);
  }
  return dealt;
}

uint8_t Dealer::DealOne() {
  if (deck_index_ >= 52) return 0xFF;
  uint8_t card = deck_[deck_index_++];
  used_stack_.push_back(card);
  return card;
}

void Dealer::Burn() {
  if (deck_index_ < 52) {
    used_stack_.push_back(deck_[deck_index_++]);
  }
}

int Dealer::Remaining() const { return 52 - deck_index_; }

void Dealer::Reset() {
  for (int i = 0; i < 52; ++i) deck_[i] = static_cast<uint8_t>(i);
  deck_index_ = 0;
  used_stack_.clear();
}

void Dealer::DealFlop(std::vector<uint8_t>& out_flop) {
  out_flop.clear();
  Burn();
  out_flop.push_back(DealOne());
  out_flop.push_back(DealOne());
  out_flop.push_back(DealOne());
}

uint8_t Dealer::DealTurn() {
  Burn();
  return DealOne();
}

uint8_t Dealer::DealRiver() {
  Burn();
  return DealOne();
}

Dealer::HandProof Dealer::GetProof() const {
  HandProof p;
  p.commitment = commitment_;
  p.seed_hex = ToHex(std::string(reinterpret_cast<const char*>(seed_.data()), seed_.size()));
  p.nonce = nonce_;
  p.deck_hash = deck_hash_;
  return p;
}

std::string Dealer::HandProof::ToString() const {
  std::ostringstream oss;
  oss << "HandProof{commitment=" << commitment << " seed=" << seed_hex << " nonce=" << nonce
      << " deck_hash=" << deck_hash << "}";
  return oss.str();
}

std::string Dealer::HandProof::ToJSON() const {
  std::ostringstream oss;
  oss << "{\"commitment\":\"" << commitment << "\""
      << ",\"seed\":\"" << seed_hex << "\""
      << ",\"nonce\":\"" << nonce << "\""
      << ",\"deck_hash\":\"" << deck_hash << "\"}";
  return oss.str();
}

std::string Dealer::ToString() const {
  std::ostringstream oss;
  oss << "Dealer{remaining=" << Remaining() << " used=" << used_stack_.size()
      << " commitment=" << commitment_ << "}";
  return oss.str();
}

}  // namespace poker_engine::game
