module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <utility>

export module fcl.quic.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.transport.api.exceptions;
import fcl.transport.api.options;
import fcl.transport.api.client;
import fcl.transport.api.connection;
import fcl.transport.api.server;
import fcl.quic.stream;
import fcl.quic.transport;
import fcl.transport.stream;

export namespace fcl::quic {

class api_binding {
 public:
   api_binding(fcl::api::binding_plan plan, fcl::transport::api::options options)
       : plan_{std::move(plan)}, options_{std::move(options)} {}

   boost::asio::awaitable<void> accept(fcl::transport::stream stream) const {
      co_await fcl::transport::api::serve_stream(std::move(stream), plan_, options_);
   }

   boost::asio::awaitable<void> accept(fcl::quic::stream stream) const {
      co_await accept(fcl::quic::as_transport_stream(std::move(stream)));
   }

   boost::asio::awaitable<void> connect(fcl::transport::stream stream) const {
      co_await accept(std::move(stream));
   }

   boost::asio::awaitable<void> connect(fcl::quic::stream stream) const {
      co_await accept(std::move(stream));
   }

   [[nodiscard]] const fcl::api::codec_id& codec() const noexcept {
      return options_.codec;
   }

   [[nodiscard]] std::size_t max_concurrent_calls() const noexcept {
      return options_.max_inflight;
   }

   [[nodiscard]] std::chrono::milliseconds deadline() const noexcept {
      return options_.deadline;
   }

   [[nodiscard]] const fcl::transport::api::options& options() const noexcept {
      return options_;
   }

 private:
   fcl::api::binding_plan plan_;
   fcl::transport::api::options options_;
};

class api_builder {
 public:
   api_builder& use(fcl::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& codec(fcl::api::codec_id value) {
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
   fcl::api::binding_plan plan_;
   fcl::transport::api::options options_{.deadline = std::chrono::milliseconds{5000}};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace fcl::quic
