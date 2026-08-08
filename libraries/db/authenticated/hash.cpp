module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module forge.db.authenticated.hash;

import forge.db.authenticated.exceptions;

namespace forge::db::authenticated {

namespace {

constexpr auto value_domain = std::string_view{"forge.db.authenticated.value.v1"};
constexpr auto leaf_domain = std::string_view{"forge.db.authenticated.leaf.v3"};
constexpr auto inner_domain = std::string_view{"forge.db.authenticated.inner.v3"};
constexpr auto empty_domain = std::string_view{"forge.db.authenticated.empty.v3"};
constexpr auto mutation_domain = std::string_view{"forge.db.authenticated.mutations.v1"};
constexpr auto state_role_tag = char{1};
constexpr auto changes_role_tag = char{2};

void write(forge::crypto::digest::sha256::encoder& encoder, std::string_view value) {
   encoder.write(value.data(), static_cast<std::uint32_t>(value.size()));
}

void write(forge::crypto::digest::sha256::encoder& encoder, std::span<const std::byte> value) {
   while (!value.empty()) {
      const auto chunk_size =
          std::min(value.size(), static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
      encoder.write(reinterpret_cast<const char*>(value.data()), static_cast<std::uint32_t>(chunk_size));
      value = value.subspan(chunk_size);
   }
}

void write(forge::crypto::digest::sha256::encoder& encoder, const digest& value) {
   encoder.write(value.data(), static_cast<std::uint32_t>(digest::data_size()));
}

void write_u16(forge::crypto::digest::sha256::encoder& encoder, std::uint16_t value) {
   auto encoded = std::array<char, 2>{
       static_cast<char>((value >> 8U) & 0xffU),
       static_cast<char>(value & 0xffU),
   };
   encoder.write(encoded.data(), static_cast<std::uint32_t>(encoded.size()));
}

void write_u32(forge::crypto::digest::sha256::encoder& encoder, std::uint32_t value) {
   auto encoded = std::array<char, 4>{};
   for (auto offset = std::size_t{}; offset < encoded.size(); ++offset) {
      encoded[offset] = static_cast<char>((value >> ((encoded.size() - offset - 1U) * 8U)) & 0xffU);
   }
   encoder.write(encoded.data(), static_cast<std::uint32_t>(encoded.size()));
}

void write_u64(forge::crypto::digest::sha256::encoder& encoder, std::uint64_t value) {
   auto encoded = std::array<char, 8>{};
   for (auto offset = std::size_t{}; offset < encoded.size(); ++offset) {
      encoded[offset] = static_cast<char>((value >> ((encoded.size() - offset - 1U) * 8U)) & 0xffU);
   }
   encoder.write(encoded.data(), static_cast<std::uint32_t>(encoded.size()));
}

void write_sized(forge::crypto::digest::sha256::encoder& encoder, std::string_view value) {
   if (value.size() > max_framed_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_store, "authenticated hash domain exceeds uint32 framing");
   }
   write_u32(encoder, static_cast<std::uint32_t>(value.size()));
   write(encoder, value);
}

void write_sized(forge::crypto::digest::sha256::encoder& encoder, std::span<const std::byte> value) {
   write_u64(encoder, value.size());
   write(encoder, value);
}

} // namespace

std::string canonical_tree_domain(std::string_view domain, proof_tree tree) {
   if (domain.empty() || domain.size() > max_base_domain_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_store, "authenticated base domain is invalid");
   }

   auto result = std::string{};
   result.reserve(domain.size() + 1U);
   switch (tree) {
   case proof_tree::state:
      result.push_back(state_role_tag);
      break;
   case proof_tree::changes:
      result.push_back(changes_role_tag);
      break;
   default:
      FORGE_THROW_EXCEPTION(exceptions::invalid_store, "authenticated tree role is invalid");
   }
   result.append(domain);
   return result;
}

digest hash_value(std::span<const std::byte> value) {
   auto encoder = digest::encoder{};
   write(encoder, value_domain);
   write_sized(encoder, value);
   return encoder.result();
}

digest hash_leaf(std::string_view domain, std::span<const std::byte> key, const digest& value_hash) {
   auto encoder = digest::encoder{};
   write(encoder, leaf_domain);
   write_sized(encoder, domain);
   write_sized(encoder, key);
   write(encoder, value_hash);
   return encoder.result();
}

digest hash_inner(std::string_view domain, std::uint16_t height, std::uint64_t subtree_size,
                  std::span<const std::byte> min_key, std::span<const std::byte> max_key,
                  std::span<const std::byte> separator, const digest& left, const digest& right) {
   auto encoder = digest::encoder{};
   write(encoder, inner_domain);
   write_sized(encoder, domain);
   write_u16(encoder, height);
   write_u64(encoder, subtree_size);
   write_sized(encoder, min_key);
   write_sized(encoder, max_key);
   write_sized(encoder, separator);
   write(encoder, left);
   write(encoder, right);
   return encoder.result();
}

digest hash_empty(std::string_view domain) {
   auto encoder = digest::encoder{};
   write(encoder, empty_domain);
   write_sized(encoder, domain);
   return encoder.result();
}

bytes encode_change_value(const mutation& value) {
   auto result = bytes{};
   result.reserve(1U + (value.value ? value.value->size() : 0U));
   result.push_back(value.value ? std::byte{1} : std::byte{0});
   if (value.value) {
      result.insert(result.end(), value.value->begin(), value.value->end());
   }
   return result;
}

std::optional<bytes> decode_change_value(std::span<const std::byte> value) {
   if (value.empty() || (value.front() != std::byte{0} && value.front() != std::byte{1}) ||
       (value.front() == std::byte{0} && value.size() != 1U)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_proof, "authenticated change value is malformed");
   }
   if (value.front() == std::byte{0}) {
      return std::nullopt;
   }
   return bytes{value.begin() + 1, value.end()};
}

digest hash_mutations(std::span<const mutation> mutations) {
   auto encoder = digest::encoder{};
   write(encoder, mutation_domain);
   write_u64(encoder, mutations.size());
   for (const auto& item : mutations) {
      write_sized(encoder, item.key);
      const auto kind = static_cast<char>(item.value.has_value() ? 1 : 0);
      encoder.write(&kind, 1U);
      if (item.value) {
         const auto value_hash = hash_value(*item.value);
         write(encoder, value_hash);
      }
   }
   return encoder.result();
}

} // namespace forge::db::authenticated
