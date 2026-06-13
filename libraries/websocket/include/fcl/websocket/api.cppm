module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

export module fcl.websocket.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.raw.raw;
import fcl.websocket.connection;
import fcl.websocket.exceptions;

export namespace fcl::websocket {

struct api_backpressure_options {
   std::size_t max_inflight = 128;
};

class api_binding {
 public:
   api_binding(fcl::api::binding_plan plan, fcl::api::codec_id codec, std::size_t max_frame_size,
               api_backpressure_options backpressure)
       : plan_{std::move(plan)}, codec_{std::move(codec)}, max_frame_size_{max_frame_size},
         backpressure_{backpressure} {}

   boost::asio::awaitable<void> accept(connection::ptr connection) const {
      install_frame_handler(std::move(connection));
      co_return;
   }

   boost::asio::awaitable<void> connect(connection::ptr connection) const {
      co_await accept(std::move(connection));
   }

   [[nodiscard]] const fcl::api::codec_id& codec() const noexcept {
      return codec_;
   }

   [[nodiscard]] std::size_t max_frame_size() const noexcept {
      return max_frame_size_;
   }

   [[nodiscard]] api_backpressure_options backpressure() const noexcept {
      return backpressure_;
   }

 private:
   void install_frame_handler(connection::ptr connection) const {
      if (!connection) {
         FCL_THROW_EXCEPTION(fcl::websocket::exceptions::closed, "websocket API binding received null connection");
      }
      if (plan_.local == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "websocket API binding has no local registry");
      }
      auto plan = plan_;
      auto codec = codec_;
      auto max_frame_size = max_frame_size_;
      auto dispatcher = std::make_shared<fcl::api::frame_dispatcher>(
          std::move(plan), fcl::api::dispatch_options{.codec = codec, .max_inflight = backpressure_.max_inflight});
      connection->on_message(
          [dispatcher = std::move(dispatcher), max_frame_size](
              fcl::websocket::connection& connection_value, std::string message)
          -> boost::asio::awaitable<void> {
             if (message.size() > max_frame_size) {
                FCL_THROW_EXCEPTION(fcl::websocket::exceptions::frame_too_large, "websocket API frame is too large");
             }
             auto request_bytes = fcl::api::bytes{message.begin(), message.end()};
             auto request = fcl::raw::unpack<fcl::api::frame>(request_bytes);
             auto responses = co_await dispatcher->dispatch(std::move(request));
             for (const auto& response : responses) {
                auto response_bytes = fcl::api::bytes{};
                fcl::raw::pack(response_bytes, response);
                co_await connection_value.send(std::string{response_bytes.begin(), response_bytes.end()});
             }
          });
   }

   fcl::api::binding_plan plan_;
   fcl::api::codec_id codec_;
   std::size_t max_frame_size_ = 1024 * 1024;
   api_backpressure_options backpressure_{};
};

class api_builder {
 public:
   api_builder& use(fcl::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& codec(fcl::api::codec_id value) {
      codec_ = std::move(value);
      return *this;
   }

   api_builder& max_frame_size(std::size_t value) {
      max_frame_size_ = value;
      return *this;
   }

   api_builder& backpressure(api_backpressure_options value) {
      backpressure_ = value;
      return *this;
   }

   [[nodiscard]] api_binding build() {
      return api_binding{std::move(plan_), std::move(codec_), max_frame_size_, backpressure_};
   }

 private:
   fcl::api::binding_plan plan_;
   fcl::api::codec_id codec_{.value = "fcl.raw"};
   std::size_t max_frame_size_ = 1024 * 1024;
   api_backpressure_options backpressure_{};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace fcl::websocket
