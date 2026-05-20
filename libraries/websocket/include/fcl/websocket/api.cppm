module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exception/macros.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

export module fcl.websocket.api;

import fcl.api;
import fcl.raw.raw;
import fcl.websocket.connection;
import fcl.websocket.exceptions;

export namespace fcl::websocket {

struct api_backpressure_options {
   std::size_t max_inflight = 128;
};

class api_binding {
 public:
   using session_handler = std::function<boost::asio::awaitable<void>(fcl::api::session&)>;

   api_binding(fcl::api::binding_plan plan, fcl::api::codec_id codec, std::size_t max_frame_size,
               api_backpressure_options backpressure)
       : plan_{std::move(plan)}, codec_{std::move(codec)}, max_frame_size_{max_frame_size},
         backpressure_{backpressure} {}

   api_binding& on_session(session_handler handler) {
      on_session_ = std::move(handler);
      return *this;
   }

   boost::asio::awaitable<fcl::api::session> accept(connection::ptr connection) const {
      auto session = make_session();
      if (on_session_) {
         co_await on_session_(session);
      }
      install_frame_handler(std::move(connection));
      co_return session;
   }

   boost::asio::awaitable<fcl::api::session> connect(connection::ptr connection) const {
      co_return co_await accept(std::move(connection));
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
   [[nodiscard]] fcl::api::session make_session() const {
      if (plan_.local == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "websocket API binding has no local registry");
      }
      return fcl::api::session{fcl::api::view{*plan_.local}};
   }

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
      auto max_inflight = backpressure_.max_inflight;
      auto calls = std::make_shared<fcl::api::call_runtime>(
          fcl::api::call_runtime_options{.max_inflight = max_inflight});
      auto streams = std::make_shared<std::unordered_map<std::uint64_t, std::vector<fcl::api::frame>>>();
      connection->on_message(
          [plan = std::move(plan), codec = std::move(codec), calls, streams, max_frame_size, max_inflight](
              fcl::websocket::connection& connection_value, std::string message) mutable
          -> boost::asio::awaitable<void> {
             if (message.size() > max_frame_size) {
                FCL_THROW_EXCEPTION(fcl::websocket::exceptions::frame_too_large, "websocket API frame is too large");
             }
             auto request_bytes = fcl::api::bytes{message.begin(), message.end()};
             auto request = fcl::raw::unpack<fcl::api::frame>(request_bytes);
             if (request.codec != codec) {
                FCL_THROW_EXCEPTION(fcl::api::exceptions::codec_failed, "websocket API frame codec is not accepted",
                                    fcl::exception::ctx("codec", request.codec.value));
             }
             const auto* descriptor = plan.local == nullptr ? nullptr : plan.local->describe(request.api);
             const auto* method = descriptor == nullptr ? nullptr : fcl::api::find_method(*descriptor, request.method);
             const auto grouped_stream =
                method != nullptr && (method->kind == fcl::api::method_kind::client_stream ||
                                      method->kind == fcl::api::method_kind::bidirectional_stream);
             if (request.kind == fcl::api::frame_kind::request && grouped_stream) {
                if (streams->size() >= max_inflight) {
                   FCL_THROW_EXCEPTION(fcl::api::exceptions::resource_exhausted,
                                       "websocket API max streams exceeded");
                }
                if (!streams->emplace(request.id.value, std::vector<fcl::api::frame>{std::move(request)}).second) {
                   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error,
                                       "duplicate active websocket API stream");
                }
                co_return;
             }
             if (auto active = streams->find(request.id.value); active != streams->end()) {
                active->second.push_back(std::move(request));
                if (active->second.back().kind != fcl::api::frame_kind::stream_end) {
                   co_return;
                }
                auto frames = std::move(active->second);
                streams->erase(active);
                auto responses = co_await plan.dispatch_stream(std::move(frames));
                for (const auto& response : responses) {
                   auto response_bytes = fcl::api::bytes{};
                   fcl::raw::pack(response_bytes, response);
                   co_await connection_value.send(std::string{response_bytes.begin(), response_bytes.end()});
                }
                co_return;
             }
             auto responses = co_await plan.dispatch_many(std::move(request), *calls);
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
   session_handler on_session_;
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
