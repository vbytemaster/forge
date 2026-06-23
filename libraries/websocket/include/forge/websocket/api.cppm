module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

export module forge.websocket.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.raw.raw;
import forge.websocket.connection;
import forge.websocket.exceptions;

export namespace forge::websocket {

struct api_backpressure_options {
   std::size_t max_inflight = 128;
};

class api_binding {
 public:
   api_binding(forge::api::binding_plan plan, forge::api::codec_id codec, std::size_t max_frame_size,
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

   [[nodiscard]] const forge::api::codec_id& codec() const noexcept {
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
         FORGE_THROW_EXCEPTION(forge::websocket::exceptions::closed, "websocket API binding received null connection");
      }
      if (plan_.local == nullptr) {
         FORGE_THROW_EXCEPTION(forge::api::exceptions::incompatible_version, "websocket API binding has no local registry");
      }
      auto plan = plan_;
      auto codec = codec_;
      auto max_frame_size = max_frame_size_;
      auto dispatcher = std::make_shared<forge::api::frame_dispatcher>(
          std::move(plan), forge::api::dispatch_options{.codec = codec, .max_inflight = backpressure_.max_inflight});
      connection->on_message(
          [dispatcher = std::move(dispatcher), max_frame_size](
              forge::websocket::connection& connection_value, std::string message)
          -> boost::asio::awaitable<void> {
             if (message.size() > max_frame_size) {
                FORGE_THROW_EXCEPTION(forge::websocket::exceptions::frame_too_large, "websocket API frame is too large");
             }
             auto request_bytes = forge::api::bytes{message.begin(), message.end()};
             auto request = forge::raw::unpack<forge::api::frame>(request_bytes);
             auto responses = co_await dispatcher->dispatch(std::move(request));
             for (const auto& response : responses) {
                auto response_bytes = forge::api::bytes{};
                forge::raw::pack(response_bytes, response);
                co_await connection_value.send(std::string{response_bytes.begin(), response_bytes.end()});
             }
          });
   }

   forge::api::binding_plan plan_;
   forge::api::codec_id codec_;
   std::size_t max_frame_size_ = 1024 * 1024;
   api_backpressure_options backpressure_{};
};

class api_builder {
 public:
   api_builder& use(forge::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& codec(forge::api::codec_id value) {
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
   forge::api::binding_plan plan_;
   forge::api::codec_id codec_{.value = "forge.raw"};
   std::size_t max_frame_size_ = 1024 * 1024;
   api_backpressure_options backpressure_{};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace forge::websocket
