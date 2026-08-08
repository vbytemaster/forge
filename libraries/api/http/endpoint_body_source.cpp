module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

module forge.api.http.binding;

import forge.api.core.exceptions;
import forge.api.core.stream_reader;
import forge.api.core.types;
import forge.net.http.body;
import forge.raw.raw;

#include "details/stream_frame_decoder.hxx"
#include "details/endpoint_body_source.hxx"

namespace forge::api::http::detail {

endpoint_body_source::endpoint_body_source(
   std::shared_ptr<forge::api::core::detail::stream_endpoint> endpoint,
   forge::api::core::stream_direction direction,
   forge::api::core::api_ref api,
   std::string method,
   forge::api::core::codec_id codec,
   std::uint32_t max_frame_bytes,
   std::uint32_t max_item_bytes)
    : endpoint_{std::move(endpoint)}, direction_{direction}, api_{std::move(api)},
      method_{std::move(method)}, codec_{std::move(codec)},
      max_frame_bytes_{max_frame_bytes}, max_item_bytes_{max_item_bytes} {
   if (!endpoint_ || max_item_bytes_ == 0 || max_item_bytes_ > max_frame_bytes_) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "HTTP API request stream source is not configured");
   }
}

boost::asio::awaitable<std::optional<forge::net::http::body_chunk>>
endpoint_body_source::async_read() {
   if (end_sent_) {
      co_return std::nullopt;
   }

   auto item = co_await endpoint_->async_read();
   auto output = forge::net::http::body_chunk{};
   if (item) {
      if (item->size() > max_item_bytes_) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                               "HTTP API request stream item exceeds the configured limit");
      }
      output = stream_frame_decoder::encode(make_item(std::move(*item)), max_frame_bytes_);
   } else {
      end_sent_ = true;
      output = stream_frame_decoder::encode(make_end(), max_frame_bytes_);
   }
   if (output.bytes.size() > std::numeric_limits<std::uint64_t>::max() - bytes_read_) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                            "HTTP API request stream byte counter overflowed");
   }
   bytes_read_ += output.bytes.size();
   co_return output;
}

std::uint64_t endpoint_body_source::bytes_read() const noexcept {
   return bytes_read_;
}

forge::api::core::frame
endpoint_body_source::make_item(forge::api::core::bytes payload) const {
   return forge::api::core::frame{
      .kind = forge::api::core::frame_kind::stream_item,
      .id = {.value = 1},
      .api = api_,
      .method = method_,
      .codec = codec_,
      .payload = std::move(payload),
   };
}

forge::api::core::frame endpoint_body_source::make_end() const {
   return forge::api::core::frame{
      .kind = forge::api::core::frame_kind::stream_end,
      .id = {.value = 1},
      .api = api_,
      .method = method_,
      .codec = codec_,
      .payload = forge::raw::pack(forge::api::core::stream_end{.direction = direction_}),
   };
}

} // namespace forge::api::http::detail
