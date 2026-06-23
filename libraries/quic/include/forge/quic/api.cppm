module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <utility>

export module forge.quic.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.transport.api.exceptions;
import forge.transport.api.options;
import forge.transport.api.client;
import forge.transport.api.connection;
import forge.transport.api.server;
import forge.quic.stream;
import forge.quic.transport;
import forge.transport.stream;

export namespace forge::quic {

class api_binding {
 public:
   api_binding(forge::api::binding_plan plan, forge::transport::api::options options)
       : plan_{std::move(plan)}, options_{std::move(options)} {}

   boost::asio::awaitable<void> accept(forge::transport::stream stream) const {
      co_await forge::transport::api::serve_stream(std::move(stream), plan_, options_);
   }

   boost::asio::awaitable<void> accept(forge::quic::stream stream) const {
      co_await accept(forge::quic::as_transport_stream(std::move(stream)));
   }

   boost::asio::awaitable<void> connect(forge::transport::stream stream) const {
      co_await accept(std::move(stream));
   }

   boost::asio::awaitable<void> connect(forge::quic::stream stream) const {
      co_await accept(std::move(stream));
   }

   [[nodiscard]] const forge::api::codec_id& codec() const noexcept {
      return options_.codec;
   }

   [[nodiscard]] std::size_t max_concurrent_calls() const noexcept {
      return options_.max_inflight;
   }

   [[nodiscard]] std::chrono::milliseconds deadline() const noexcept {
      return options_.deadline;
   }

   [[nodiscard]] const forge::transport::api::options& options() const noexcept {
      return options_;
   }

 private:
   forge::api::binding_plan plan_;
   forge::transport::api::options options_;
};

class api_builder {
 public:
   api_builder& use(forge::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& codec(forge::api::codec_id value) {
      options_.codec = std::move(value);
      return *this;
   }

   api_builder& max_concurrent_calls(std::size_t value) {
      options_.max_inflight = value;
      return *this;
   }

   api_builder& deadline(std::chrono::milliseconds value) {
      options_.deadline = value;
      return *this;
   }

   api_builder& max_frame_size(std::uint32_t value) {
      options_.max_frame_size = value;
      return *this;
   }

   [[nodiscard]] api_binding build() {
      return api_binding{std::move(plan_), options_};
   }

 private:
   forge::api::binding_plan plan_;
   forge::transport::api::options options_{.deadline = std::chrono::milliseconds{5000}};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace forge::quic
