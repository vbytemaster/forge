module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <utility>

export module fcl.quic.api;

import fcl.api;
import fcl.api.transport;
import fcl.quic.stream;
import fcl.quic.transport;
import fcl.transport.stream;

export namespace fcl::quic {

class api_binding {
 public:
   api_binding(fcl::api::binding_plan plan, fcl::api::transport::options options)
       : plan_{std::move(plan)}, options_{std::move(options)} {}

   boost::asio::awaitable<void> accept(fcl::transport::stream stream) const {
      co_await fcl::api::transport::serve_stream(std::move(stream), plan_, options_);
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

   [[nodiscard]] const fcl::api::transport::options& options() const noexcept {
      return options_;
   }

 private:
   fcl::api::binding_plan plan_;
   fcl::api::transport::options options_;
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
   fcl::api::transport::options options_{.deadline = std::chrono::milliseconds{5000}};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace fcl::quic
