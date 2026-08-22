#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>

namespace forge::net::quic::detail {

struct initial_token_remote_address {
   const ngtcp2_sockaddr* address = nullptr;
   ngtcp2_socklen length = 0;
};

enum class initial_token_disposition {
   retry,
   accept,
   reject_invalid,
   internal_failure,
};

struct initial_token_validation {
   initial_token_disposition disposition = initial_token_disposition::retry;
   ngtcp2_cid original_dcid{};
   ngtcp2_token_type token_type = NGTCP2_TOKEN_TYPE_UNKNOWN;

   [[nodiscard]] bool accepted() const noexcept {
      return disposition == initial_token_disposition::accept;
   }
};

class initial_token_validator {
 public:
   static constexpr auto secret_size = std::size_t{32};
   using secret = std::array<std::uint8_t, secret_size>;

   initial_token_validator(secret value, ngtcp2_duration retry_lifetime, ngtcp2_duration regular_lifetime) noexcept;

   [[nodiscard]] std::optional<std::vector<std::uint8_t>>
   generate_retry(std::uint32_t version, initial_token_remote_address remote, const ngtcp2_cid& retry_scid,
                  const ngtcp2_cid& original_dcid, ngtcp2_tstamp now) const;
   [[nodiscard]] std::optional<std::vector<std::uint8_t>> generate_regular(initial_token_remote_address remote,
                                                                           ngtcp2_tstamp now) const;
   [[nodiscard]] initial_token_validation validate(std::span<const std::uint8_t> token, std::uint32_t version,
                                                   initial_token_remote_address remote, const ngtcp2_cid& dcid,
                                                   ngtcp2_tstamp now) const;

 private:
   secret secret_;
   ngtcp2_duration retry_lifetime_ = 0;
   ngtcp2_duration regular_lifetime_ = 0;
};

} // namespace forge::net::quic::detail
