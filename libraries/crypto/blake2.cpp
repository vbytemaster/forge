module;
#include <forge/exceptions/macros.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

module forge.crypto.blake2;

import forge.core.utility;

namespace forge::crypto {
namespace {

constexpr std::array<std::uint64_t, 8> blake2b_iv = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

constexpr std::uint8_t blake2b_sigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}, {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}, {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}, {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}, {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

std::uint64_t load64(const void* src) {
   std::uint64_t value = 0;
   std::memcpy(&value, src, sizeof(value));
   return value;
}

std::uint64_t rotr64(std::uint64_t word, unsigned count) noexcept {
   return (word >> count) | (word << (64U - count));
}

struct blake2b_state {
   std::uint64_t h[8] = {};
   std::uint64_t t[2] = {};
   std::uint64_t f[1] = {};
};

class blake2b_wrapper {
 public:
   static constexpr std::size_t block_bytes = 128;

   void compress(blake2b_state* state, const std::uint8_t block[block_bytes], std::size_t rounds,
                 const yield_function_t& yield);

 private:
   std::uint64_t m_[16] = {};
   std::uint64_t v_[16] = {};
   std::size_t index_ = 0;

   void mix(std::uint8_t round, std::uint8_t index, std::uint64_t& a, std::uint64_t& b, std::uint64_t& c,
            std::uint64_t& d) noexcept;
   void round(std::uint8_t round) noexcept;
   void init(blake2b_state* state, const std::uint8_t block[block_bytes]);
   void finish(blake2b_state* state);
};

void blake2b_wrapper::mix(std::uint8_t round, std::uint8_t index, std::uint64_t& a, std::uint64_t& b, std::uint64_t& c,
                          std::uint64_t& d) noexcept {
   a = a + b + m_[blake2b_sigma[round][2 * index + 0]];
   d = rotr64(d ^ a, 32);
   c = c + d;
   b = rotr64(b ^ c, 24);
   a = a + b + m_[blake2b_sigma[round][2 * index + 1]];
   d = rotr64(d ^ a, 16);
   c = c + d;
   b = rotr64(b ^ c, 63);
}

void blake2b_wrapper::round(std::uint8_t round) noexcept {
   mix(round, 0, v_[0], v_[4], v_[8], v_[12]);
   mix(round, 1, v_[1], v_[5], v_[9], v_[13]);
   mix(round, 2, v_[2], v_[6], v_[10], v_[14]);
   mix(round, 3, v_[3], v_[7], v_[11], v_[15]);
   mix(round, 4, v_[0], v_[5], v_[10], v_[15]);
   mix(round, 5, v_[1], v_[6], v_[11], v_[12]);
   mix(round, 6, v_[2], v_[7], v_[8], v_[13]);
   mix(round, 7, v_[3], v_[4], v_[9], v_[14]);
}

void blake2b_wrapper::compress(blake2b_state* state, const std::uint8_t block[block_bytes], std::size_t rounds,
                               const yield_function_t& yield) {
   init(state, block);

   for (index_ = 0; index_ < rounds; ++index_) {
      round(static_cast<std::uint8_t>(index_ % 10));
      if (index_ % 100) {
         yield();
      }
   }

   finish(state);
}

void blake2b_wrapper::init(blake2b_state* state, const std::uint8_t block[block_bytes]) {
   for (index_ = 0; index_ < 16; ++index_) {
      m_[index_] = load64(block + index_ * sizeof(m_[index_]));
   }

   for (index_ = 0; index_ < 8; ++index_) {
      v_[index_] = state->h[index_];
   }

   v_[8] = blake2b_iv[0];
   v_[9] = blake2b_iv[1];
   v_[10] = blake2b_iv[2];
   v_[11] = blake2b_iv[3];
   v_[12] = blake2b_iv[4] ^ state->t[0];
   v_[13] = blake2b_iv[5] ^ state->t[1];
   v_[14] = blake2b_iv[6] ^ state->f[0];
   v_[15] = blake2b_iv[7];
}

void blake2b_wrapper::finish(blake2b_state* state) {
   for (index_ = 0; index_ < 8; ++index_) {
      state->h[index_] = state->h[index_] ^ v_[index_] ^ v_[index_ + 8];
   }
}

} // namespace

bytes blake2b(std::uint32_t rounds, const bytes& h, const bytes& m, const bytes& t0_offset, const bytes& t1_offset,
              bool final_block, const yield_function_t& yield) {
   if (h.size() != 64 || m.size() != blake2b_wrapper::block_bytes || t0_offset.size() != 8 || t1_offset.size() != 8) {
      FORGE_THROW_EXCEPTION(blake2::exceptions::invalid_input, "invalid BLAKE2b compression input length");
   }

   blake2b_wrapper wrapper;
   blake2b_state state{};

   std::memcpy(state.h, h.data(), 64);
   state.f[0] = final_block ? std::numeric_limits<std::uint64_t>::max() : 0;

   std::memcpy(&state.t[0], t0_offset.data(), 8);
   std::memcpy(&state.t[1], t1_offset.data(), 8);

   std::uint8_t block[blake2b_wrapper::block_bytes] = {};
   std::memcpy(block, m.data(), blake2b_wrapper::block_bytes);

   wrapper.compress(&state, block, rounds, yield);

   bytes out(sizeof(state.h), 0);
   std::memcpy(out.data(), state.h, out.size());
   return out;
}

} // namespace forge::crypto
