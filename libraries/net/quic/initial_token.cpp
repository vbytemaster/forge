#include "details/initial_token.hxx"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forge::net::quic::detail {
namespace {

[[nodiscard]] initial_token_validation retry() noexcept {
   return {.disposition = initial_token_disposition::retry};
}

[[nodiscard]] initial_token_validation reject_invalid() noexcept {
   return {.disposition = initial_token_disposition::reject_invalid};
}

[[nodiscard]] initial_token_validation internal_failure() noexcept {
   return {.disposition = initial_token_disposition::internal_failure};
}

[[nodiscard]] initial_token_validation accept_retry(const ngtcp2_cid& original_dcid) noexcept {
   return {
       .disposition = initial_token_disposition::accept,
       .original_dcid = original_dcid,
       .token_type = NGTCP2_TOKEN_TYPE_RETRY,
   };
}

[[nodiscard]] initial_token_validation accept_regular(const ngtcp2_cid& dcid) noexcept {
   return {
       .disposition = initial_token_disposition::accept,
       .original_dcid = dcid,
       .token_type = NGTCP2_TOKEN_TYPE_NEW_TOKEN,
   };
}

} // namespace

initial_token_validator::initial_token_validator(secret value, ngtcp2_duration retry_lifetime,
                                                 ngtcp2_duration regular_lifetime) noexcept
    : secret_(value), retry_lifetime_(retry_lifetime), regular_lifetime_(regular_lifetime) {}

std::optional<std::vector<std::uint8_t>> initial_token_validator::generate_retry(std::uint32_t version,
                                                                                 initial_token_remote_address remote,
                                                                                 const ngtcp2_cid& retry_scid,
                                                                                 const ngtcp2_cid& original_dcid,
                                                                                 ngtcp2_tstamp now) const {
   if (remote.address == nullptr || remote.length == 0) {
      return std::nullopt;
   }
   auto token = std::vector<std::uint8_t>(NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2);
   const auto length =
       ngtcp2_crypto_generate_retry_token2(token.data(), secret_.data(), secret_.size(), version, remote.address,
                                           remote.length, &retry_scid, &original_dcid, now);
   if (length < 0 || static_cast<std::size_t>(length) > token.size()) {
      return std::nullopt;
   }
   token.resize(static_cast<std::size_t>(length));
   return token;
}

std::optional<std::vector<std::uint8_t>> initial_token_validator::generate_regular(initial_token_remote_address remote,
                                                                                   ngtcp2_tstamp now) const {
   if (remote.address == nullptr || remote.length == 0) {
      return std::nullopt;
   }
   auto token = std::vector<std::uint8_t>(NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN);
   const auto length = ngtcp2_crypto_generate_regular_token(token.data(), secret_.data(), secret_.size(),
                                                            remote.address, remote.length, now);
   if (length < 0 || static_cast<std::size_t>(length) > token.size()) {
      return std::nullopt;
   }
   token.resize(static_cast<std::size_t>(length));
   return token;
}

initial_token_validation initial_token_validator::validate(std::span<const std::uint8_t> token, std::uint32_t version,
                                                           initial_token_remote_address remote, const ngtcp2_cid& dcid,
                                                           ngtcp2_tstamp now) const {
   if (token.empty()) {
      return retry();
   }
   if (remote.address == nullptr || remote.length == 0) {
      return internal_failure();
   }

   switch (token.front()) {
   case NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2: {
      auto original_dcid = ngtcp2_cid{};
      const auto rv =
          ngtcp2_crypto_verify_retry_token2(&original_dcid, token.data(), token.size(), secret_.data(), secret_.size(),
                                            version, remote.address, remote.length, &dcid, retry_lifetime_, now);
      if (rv == 0) {
         return accept_retry(original_dcid);
      }
      if (rv == NGTCP2_CRYPTO_ERR_UNREADABLE_TOKEN) {
         return retry();
      }
      if (rv == NGTCP2_CRYPTO_ERR_VERIFY_TOKEN) {
         return reject_invalid();
      }
      if (rv == NGTCP2_CRYPTO_ERR_INTERNAL) {
         return internal_failure();
      }
      return internal_failure();
   }
   case NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY: {
      auto original_dcid = ngtcp2_cid{};
      if (ngtcp2_crypto_verify_retry_token(&original_dcid, token.data(), token.size(), secret_.data(), secret_.size(),
                                           version, remote.address, remote.length, &dcid, retry_lifetime_, now) == 0) {
         return accept_retry(original_dcid);
      }
      return retry();
   }
   case NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR: {
      const auto rv =
          ngtcp2_crypto_verify_regular_token2(nullptr, 0, token.data(), token.size(), secret_.data(), secret_.size(),
                                              remote.address, remote.length, regular_lifetime_, now);
      if (rv >= 0) {
         return accept_regular(dcid);
      }
      if (rv == NGTCP2_CRYPTO_ERR_INTERNAL) {
         return internal_failure();
      }
      return retry();
   }
   default:
      return retry();
   }
}

} // namespace forge::net::quic::detail
