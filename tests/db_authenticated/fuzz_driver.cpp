#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <span>

import forge.db.authenticated.codec;
import forge.db.authenticated.proof;
import forge.db.authenticated.types;
import forge.exceptions;

namespace {

using namespace forge::db::authenticated;

constexpr auto fuzz_limits = limits{
    .max_key_bytes = 4U << 10U,
    .max_value_bytes = 64U << 10U,
    .max_proof_bytes = 1U << 20U,
    .max_proof_depth = 128,
    .max_proof_nodes = 4'096,
    .max_range_items = 1'024,
};

void fuzz_point(std::span<const std::byte> input) {
   const auto proof = decode_point(input, fuzz_limits);
   const auto encoded = encode(proof);
   if (decode_point(encoded, fuzz_limits) != proof || wire_size(proof) != encoded.size()) {
      std::abort();
   }
   static_cast<void>(verify_point("forge.db.authenticated.fuzz", proof.anchor, proof.key, proof, fuzz_limits));
}

void fuzz_range(std::span<const std::byte> input) {
   const auto proof = decode_range(input, fuzz_limits);
   const auto encoded = encode(proof);
   if (decode_range(encoded, fuzz_limits) != proof || wire_size(proof) != encoded.size()) {
      std::abort();
   }
   static_cast<void>(
       verify_range("forge.db.authenticated.fuzz", proof.anchor, proof.request, proof.tree, proof, fuzz_limits));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
   if (size == 0U) {
      return 0;
   }
   const auto input = std::span<const std::byte>{reinterpret_cast<const std::byte*>(data + 1U), size - 1U};
   try {
      if ((data[0] & 1U) == 0U) {
         fuzz_point(input);
      } else {
         fuzz_range(input);
      }
   } catch (const forge::exceptions::base&) {
      // Rejected encodings and invalid proofs are expected fuzz inputs.
   } catch (const std::exception&) {
      // Standard allocation and conversion failures are also expected for hostile input.
   }
   return 0;
}
