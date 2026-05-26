module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exception/macros.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

export module fcl.quic.api;

import fcl.api;
import fcl.raw.raw;
import fcl.quic.exceptions;
import fcl.quic.framed_stream;

export namespace fcl::quic {

enum class api_stream_policy {
   one_stream_per_call,
   multiplexed,
};

class api_binding {
 public:
   using session_handler = std::function<boost::asio::awaitable<void>(fcl::api::session&)>;

   api_binding(fcl::api::binding_plan plan, fcl::api::codec_id codec, api_stream_policy stream_policy,
               std::size_t max_concurrent_calls, std::chrono::milliseconds deadline)
       : plan_{std::move(plan)}, codec_{std::move(codec)}, stream_policy_{stream_policy},
         max_concurrent_calls_{max_concurrent_calls}, deadline_{deadline} {}

   api_binding& on_session(session_handler handler) {
      on_session_ = std::move(handler);
      return *this;
   }

   boost::asio::awaitable<fcl::api::session> accept(fcl::quic::framed_stream stream) const {
      auto session = make_session();
      if (on_session_) {
         co_await on_session_(session);
      }
      if (stream_policy_ == api_stream_policy::one_stream_per_call) {
         co_await serve_once(std::move(stream));
      } else {
         co_await serve(std::move(stream));
      }
      co_return session;
   }

   boost::asio::awaitable<fcl::api::session> connect(fcl::quic::framed_stream stream) const {
      co_return co_await accept(std::move(stream));
   }

   boost::asio::awaitable<void> serve_once(fcl::quic::framed_stream stream) const {
      if (plan_.local == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "QUIC API binding has no local registry");
      }
      auto calls = fcl::api::call_runtime{
          fcl::api::call_runtime_options{.max_inflight = max_concurrent_calls_, .deadline = deadline_}};
      auto payload = co_await stream.async_read_frame();
      auto request = fcl::raw::unpack<fcl::api::frame>(payload);
      if (request.codec != codec_) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::codec_failed, "QUIC API frame codec is not accepted",
                             fcl::exception::ctx("codec", request.codec.value));
      }
      if (grouped_stream_method(request)) {
         calls.observe(request);
         auto frames = std::vector<fcl::api::frame>{std::move(request)};
         while (frames.back().kind != fcl::api::frame_kind::stream_end) {
            auto next_payload = co_await stream.async_read_frame();
            auto next = fcl::raw::unpack<fcl::api::frame>(next_payload);
            if (next.codec != codec_) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::codec_failed, "QUIC API frame codec is not accepted",
                                   fcl::exception::ctx("codec", next.codec.value));
            }
            if (next.kind != fcl::api::frame_kind::stream_end) {
               calls.observe(next);
            }
            frames.push_back(std::move(next));
         }
         auto responses = co_await plan_.dispatch_stream(std::move(frames), calls);
         co_await write_responses(stream, responses);
         co_return;
      }
      auto responses = co_await plan_.dispatch_many(std::move(request), calls);
      co_await write_responses(stream, responses);
   }

   boost::asio::awaitable<void> serve(fcl::quic::framed_stream stream) const {
      if (plan_.local == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "QUIC API binding has no local registry");
      }
      auto calls = fcl::api::call_runtime{
          fcl::api::call_runtime_options{.max_inflight = max_concurrent_calls_, .deadline = deadline_}};
      auto streams = std::unordered_map<std::uint64_t, std::vector<fcl::api::frame>>{};

      while (true) {
         try {
            auto payload = co_await stream.async_read_frame();
            auto request = fcl::raw::unpack<fcl::api::frame>(payload);
            if (request.codec != codec_) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::codec_failed, "QUIC API frame codec is not accepted",
                                   fcl::exception::ctx("codec", request.codec.value));
            }
            if (request.kind == fcl::api::frame_kind::request && grouped_stream_method(request)) {
               calls.observe(request);
               if (streams.size() >= max_concurrent_calls_) {
                  FCL_THROW_EXCEPTION(fcl::api::exceptions::resource_exhausted, "QUIC API max streams exceeded");
               }
               if (!streams.emplace(request.id.value, std::vector<fcl::api::frame>{std::move(request)}).second) {
                  FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "duplicate active QUIC API stream");
               }
               continue;
            }
            if (auto active = streams.find(request.id.value); active != streams.end()) {
               if (request.kind != fcl::api::frame_kind::stream_end) {
                  calls.observe(request);
               }
               active->second.push_back(std::move(request));
               if (active->second.back().kind == fcl::api::frame_kind::stream_end) {
                  auto frames = std::move(active->second);
                  streams.erase(active);
                  auto responses = co_await plan_.dispatch_stream(std::move(frames), calls);
                  co_await write_responses(stream, responses);
               }
               continue;
            }
            auto responses = co_await plan_.dispatch_many(std::move(request), calls);
            co_await write_responses(stream, responses);
         } catch (const fcl::exception::base& error) {
            if (fcl::quic::exceptions::is(error, fcl::quic::exceptions::code::connection_closed) ||
                fcl::quic::exceptions::is(error, fcl::quic::exceptions::code::stream_closed) ||
                fcl::quic::exceptions::is(error, fcl::quic::exceptions::code::stream_reset) ||
                fcl::quic::exceptions::is(error, fcl::quic::exceptions::code::canceled)) {
               co_return;
            }
            throw;
         }
      }
   }

   [[nodiscard]] const fcl::api::codec_id& codec() const noexcept {
      return codec_;
   }

   [[nodiscard]] api_stream_policy stream_policy() const noexcept {
      return stream_policy_;
   }

   [[nodiscard]] std::size_t max_concurrent_calls() const noexcept {
      return max_concurrent_calls_;
   }

   [[nodiscard]] std::chrono::milliseconds deadline() const noexcept {
      return deadline_;
   }

 private:
   [[nodiscard]] bool grouped_stream_method(const fcl::api::frame& request) const noexcept {
      const auto* descriptor = plan_.local == nullptr ? nullptr : plan_.local->describe(request.api);
      const auto* method = descriptor == nullptr ? nullptr : fcl::api::find_method(*descriptor, request.method);
      return method != nullptr && (method->kind == fcl::api::method_kind::client_stream ||
                                   method->kind == fcl::api::method_kind::bidirectional_stream);
   }

   static boost::asio::awaitable<void> write_responses(fcl::quic::framed_stream& stream,
                                                       const std::vector<fcl::api::frame>& responses) {
      for (const auto& response : responses) {
         auto response_bytes = fcl::api::bytes{};
         fcl::raw::pack(response_bytes, response);
         co_await stream.async_write_frame(std::span<const std::uint8_t>{response_bytes.data(), response_bytes.size()});
      }
   }

   [[nodiscard]] fcl::api::session make_session() const {
      if (plan_.local == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "QUIC API binding has no local registry");
      }
      return fcl::api::session{fcl::api::view{*plan_.local}};
   }

   fcl::api::binding_plan plan_;
   fcl::api::codec_id codec_;
   api_stream_policy stream_policy_ = api_stream_policy::one_stream_per_call;
   std::size_t max_concurrent_calls_ = 256;
   std::chrono::milliseconds deadline_{5000};
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

   api_builder& stream_policy(api_stream_policy value) {
      stream_policy_ = value;
      return *this;
   }

   api_builder& max_concurrent_calls(std::size_t value) {
      max_concurrent_calls_ = value;
      return *this;
   }

   api_builder& deadline(std::chrono::milliseconds value) {
      deadline_ = value;
      return *this;
   }

   [[nodiscard]] api_binding build() {
      return api_binding{std::move(plan_), std::move(codec_), stream_policy_, max_concurrent_calls_, deadline_};
   }

 private:
   fcl::api::binding_plan plan_;
   fcl::api::codec_id codec_{.value = "fcl.raw"};
   api_stream_policy stream_policy_ = api_stream_policy::one_stream_per_call;
   std::size_t max_concurrent_calls_ = 256;
   std::chrono::milliseconds deadline_{5000};
};

[[nodiscard]] inline api_builder api() {
   return {};
}

} // namespace fcl::quic
