module;

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module fcl.p2p.message;

import fcl.api.types;
import fcl.p2p.identity;
import fcl.p2p.protocol;
import fcl.quic.endpoint;
import fcl.raw.raw;

export namespace fcl::p2p {

inline constexpr std::uint16_t wire_version_v1 = 1;
inline constexpr std::uint32_t mandatory_flag_mask = 0x8000'0000U;

struct control_message {
   enum class type : std::uint16_t {
      hello = 1,
      hello_ack = 2,
      protocol_open = 3,
      protocol_accept = 4,
      protocol_reject = 5,
      peer_exchange_request = 6,
      peer_exchange_response = 7,
      relay_open = 8,
      relay_accept = 9,
      relay_reject = 10,
      relay_close = 11,
      ping = 12,
      pong = 13,
      goaway = 14,
      reachability_probe = 15,
      reachability_result = 16,
      relay_reserve = 17,
      relay_renew = 18,
      relay_cancel = 19,
      relay_reserved = 20,
      hole_punch_prepare = 21,
      hole_punch_sync = 22,
      hole_punch_result = 23
   };

   struct endpoint_record {
      peer_id peer;
      fcl::quic::endpoint endpoint;
      capability_set capabilities{};
   };

   type kind = type::ping;
   std::uint64_t request_id = 0;
   std::uint32_t flags = 0;
   peer_id peer;
   protocol_id protocol;
   capability_set capabilities{};
   std::uint64_t max_frame_size = 16 * 1024 * 1024;
   std::uint64_t reservation_id = 0;
   std::uint64_t ttl_ms = 0;
   std::uint64_t max_streams = 0;
   std::uint64_t max_bytes = 0;
   std::uint64_t max_queued_bytes = 0;
   reachability_state reachability = reachability_state::unknown;
   hole_punch_status hole_punch = hole_punch_status::not_attempted;
   std::string reason;
   std::vector<endpoint_record> endpoints;
   std::vector<std::uint8_t> payload;
};

class message {
 public:
   struct options {
      fcl::api::codec_id codec{.value = "fcl.raw"};
      fcl::api::metadata meta;
   };

   message() = default;

   message(protocol_id protocol, fcl::api::bytes data) : message{std::move(protocol), std::move(data), options{}} {}

   message(protocol_id protocol, fcl::api::bytes data, options value)
       : protocol_{std::move(protocol)}, codec_{std::move(value.codec)}, meta_{std::move(value.meta)},
         data_{std::move(data)} {}

   template <typename T> message(protocol_id protocol, const T& value) : message{std::move(protocol), value, options{}} {}

   template <typename T> message(protocol_id protocol, const T& value, options opts)
       : protocol_{std::move(protocol)}, codec_{std::move(opts.codec)}, meta_{std::move(opts.meta)} {
      require_raw_codec();
      fcl::raw::pack(data_, value);
   }

   template <typename T> [[nodiscard]] T as() const {
      require_raw_codec();
      return fcl::raw::unpack<T>(std::span<const std::uint8_t>{data_.data(), data_.size()});
   }

   [[nodiscard]] const protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] const fcl::api::codec_id& codec() const noexcept {
      return codec_;
   }

   [[nodiscard]] const fcl::api::metadata& meta() const noexcept {
      return meta_;
   }

   [[nodiscard]] const fcl::api::bytes& data() const noexcept {
      return data_;
   }

 private:
   void require_raw_codec() const {
      if (codec_.value != "fcl.raw") {
         throw std::invalid_argument{"typed P2P message construction requires fcl.raw codec"};
      }
   }

   protocol_id protocol_;
   fcl::api::codec_id codec_{.value = "fcl.raw"};
   fcl::api::metadata meta_;
   fcl::api::bytes data_;
};

} // namespace fcl::p2p
