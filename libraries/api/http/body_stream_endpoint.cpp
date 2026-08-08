module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

module forge.api.http.binding;

import forge.api.core.exceptions;
import forge.api.core.stream_reader;
import forge.api.core.types;
import forge.net.http.body;
import forge.raw.raw;

#include "details/stream_frame_decoder.hxx"
#include "details/body_stream_endpoint.hxx"

namespace forge::api::http::detail {

body_stream_endpoint::body_stream_endpoint(
   forge::net::http::body_reader body,
   forge::api::core::stream_direction direction,
   decoder item_decoder,
   std::uint32_t max_frame_bytes,
   std::uint32_t max_item_bytes,
   bool terminal_required)
    : decoder_{std::move(body), max_frame_bytes},
      item_decoder_{std::move(item_decoder)}, direction_{direction},
      max_item_bytes_{max_item_bytes}, terminal_required_{terminal_required} {
   if (max_item_bytes_ == 0 || max_item_bytes_ > max_frame_bytes) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "HTTP API stream item limit is invalid");
   }
}

boost::asio::awaitable<std::optional<forge::api::core::bytes>>
body_stream_endpoint::async_read() {
   rethrow_failure();
   for (;;) {
      auto next = co_await decoder_.async_read();
      if (!next) {
         const auto lock = std::scoped_lock{mutex_};
         if (terminal_required_ && !terminal_) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API stream ended without a terminal frame");
         }
         if (!ended_.load(std::memory_order_acquire)) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API stream ended without stream_end");
         }
         co_return std::nullopt;
      }
      validate_call_frame(*next);
      switch (next->kind) {
      case forge::api::core::frame_kind::stream_item:
         if (ended_.load(std::memory_order_acquire)) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API stream item followed stream_end");
         }
         if (next->payload.size() > max_item_bytes_) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                                  "HTTP API stream item exceeds the configured limit");
         }
         if (item_decoder_) {
            item_decoder_(next->payload, payload_limits(next->payload.size()));
         }
         co_return std::move(next->payload);
      case forge::api::core::frame_kind::stream_end: {
         const auto end = forge::raw::unpack_exact<forge::api::core::stream_end>(
            next->payload, payload_limits(next->payload.size()));
         if (ended_.exchange(true, std::memory_order_acq_rel) ||
             end.direction != direction_) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API stream_end has the wrong direction or is duplicated");
         }
         co_return std::nullopt;
      }
      case forge::api::core::frame_kind::response:
      case forge::api::core::frame_kind::error:
         if (!terminal_required_ || !ended_.load(std::memory_order_acquire)) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API terminal frame arrived before stream_end");
         }
         remember_terminal(std::move(*next));
         co_return std::nullopt;
      default:
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                               "HTTP API stream body contains a forbidden frame kind");
      }
   }
}

boost::asio::awaitable<void>
body_stream_endpoint::async_write(forge::api::core::bytes) {
   FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                         "HTTP request body stream is read-only");
}

void body_stream_endpoint::close() noexcept {
   ended_.store(true, std::memory_order_release);
}

void body_stream_endpoint::fail(std::exception_ptr error) noexcept {
   auto cancel_body = false;
   {
      const auto lock = std::scoped_lock{mutex_};
      if (!failure_) {
         failure_ = std::move(error);
         cancel_body = true;
      }
   }
   if (cancel_body) {
      decoder_.cancel();
   }
}

boost::asio::awaitable<forge::api::core::frame>
body_stream_endpoint::async_finish() {
   auto result = std::optional<forge::api::core::frame>{};
   for (;;) {
      rethrow_failure();
      {
         const auto lock = std::scoped_lock{mutex_};
         if (terminal_) {
            result = std::move(*terminal_);
            terminal_.reset();
         }
      }
      if (result) {
         if (co_await decoder_.async_read()) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API stream has trailing frames after the terminal frame");
         }
         co_return std::move(*result);
      }
      auto next = co_await decoder_.async_read();
      if (!next) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                               "HTTP API stream ended without a terminal frame");
      }
      validate_call_frame(*next);
      if (next->kind != forge::api::core::frame_kind::response &&
          next->kind != forge::api::core::frame_kind::error) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                               "HTTP API stream contains data after stream_end");
      }
      remember_terminal(std::move(*next));
   }
}

forge::raw::unpack_limits
body_stream_endpoint::payload_limits(std::size_t size) const noexcept {
   const auto bounded = static_cast<std::uint32_t>(
      std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
   return forge::raw::unpack_limits{
      .max_container_elements = bounded,
      .max_total_container_elements = bounded,
      .max_bytes = bounded,
      .first_container_elements = bounded,
   };
}

void body_stream_endpoint::validate_call_frame(
   const forge::api::core::frame& value) const {
   if (value.id.value != 1) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "HTTP API stream frame has an invalid call_id");
   }
}

void body_stream_endpoint::remember_terminal(forge::api::core::frame value) {
   const auto lock = std::scoped_lock{mutex_};
   if (terminal_) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "HTTP API stream has duplicate terminal frames");
   }
   terminal_ = std::move(value);
}

void body_stream_endpoint::rethrow_failure() const {
   auto failure = std::exception_ptr{};
   {
      const auto lock = std::scoped_lock{mutex_};
      failure = failure_;
   }
   if (failure) {
      std::rethrow_exception(failure);
   }
}

} // namespace forge::api::http::detail
