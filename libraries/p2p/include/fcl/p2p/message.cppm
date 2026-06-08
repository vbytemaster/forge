module;

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module fcl.p2p.message;

import fcl.api.types;
import fcl.p2p.protocol;
import fcl.raw.raw;

export namespace fcl::p2p {

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
