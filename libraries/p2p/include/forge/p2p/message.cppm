module;

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module forge.p2p.message;

import forge.api.types;
import forge.p2p.protocol;
import forge.raw.raw;

export namespace forge::p2p {

class message {
 public:
   struct options {
      forge::api::codec_id codec{.value = "forge.raw"};
      forge::api::metadata meta;
   };

   message() = default;

   message(protocol_id protocol, forge::api::bytes data) : message{std::move(protocol), std::move(data), options{}} {}

   message(protocol_id protocol, forge::api::bytes data, options value)
       : protocol_{std::move(protocol)}, codec_{std::move(value.codec)}, meta_{std::move(value.meta)},
         data_{std::move(data)} {}

   template <typename T> message(protocol_id protocol, const T& value) : message{std::move(protocol), value, options{}} {}

   template <typename T> message(protocol_id protocol, const T& value, options opts)
       : protocol_{std::move(protocol)}, codec_{std::move(opts.codec)}, meta_{std::move(opts.meta)} {
      require_raw_codec();
      forge::raw::pack(data_, value);
   }

   template <typename T> [[nodiscard]] T as() const {
      require_raw_codec();
      return forge::raw::unpack<T>(std::span<const std::uint8_t>{data_.data(), data_.size()});
   }

   [[nodiscard]] const protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] const forge::api::codec_id& codec() const noexcept {
      return codec_;
   }

   [[nodiscard]] const forge::api::metadata& meta() const noexcept {
      return meta_;
   }

   [[nodiscard]] const forge::api::bytes& data() const noexcept {
      return data_;
   }

 private:
   void require_raw_codec() const {
      if (codec_.value != "forge.raw") {
         throw std::invalid_argument{"typed P2P message construction requires forge.raw codec"};
      }
   }

   protocol_id protocol_;
   forge::api::codec_id codec_{.value = "forge.raw"};
   forge::api::metadata meta_;
   forge::api::bytes data_;
};

} // namespace forge::p2p
