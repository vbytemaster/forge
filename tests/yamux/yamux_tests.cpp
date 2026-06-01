#include <boost/test/unit_test.hpp>

#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.transport.exceptions;
import fcl.transport.stream;
import fcl.yamux.exceptions;
import fcl.yamux.options;
import fcl.yamux.session;

namespace {

using bytes = std::vector<std::uint8_t>;

enum class frame_type : std::uint8_t {
   data = 0,
   window_update = 1,
   ping = 2,
   go_away = 3,
};

std::ostream& operator<<(std::ostream& out, frame_type value) {
   return out << static_cast<int>(value);
}

inline constexpr std::uint16_t syn = 0x01;
inline constexpr std::uint16_t ack = 0x02;
inline constexpr std::uint16_t rst = 0x08;
inline constexpr std::size_t header_size = 12;

[[nodiscard]] bytes text_bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] bytes frame(frame_type type, std::uint16_t flags, std::uint32_t stream_id, std::uint32_t length,
                          std::span<const std::uint8_t> payload = {}) {
   auto out = bytes{};
   out.reserve(header_size + payload.size());
   out.push_back(0);
   out.push_back(static_cast<std::uint8_t>(type));
   out.push_back(static_cast<std::uint8_t>((flags >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(flags & 0xffU));
   for (auto shift : {24, 16, 8, 0}) {
      out.push_back(static_cast<std::uint8_t>((stream_id >> shift) & 0xffU));
   }
   for (auto shift : {24, 16, 8, 0}) {
      out.push_back(static_cast<std::uint8_t>((length >> shift) & 0xffU));
   }
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

[[nodiscard]] std::uint32_t load_u32(const bytes& value, std::size_t offset) {
   BOOST_REQUIRE_GE(value.size(), offset + 4);
   return (static_cast<std::uint32_t>(value[offset]) << 24U) |
          (static_cast<std::uint32_t>(value[offset + 1]) << 16U) |
          (static_cast<std::uint32_t>(value[offset + 2]) << 8U) |
          static_cast<std::uint32_t>(value[offset + 3]);
}

[[nodiscard]] frame_type type_of(const bytes& value) {
   BOOST_REQUIRE_GE(value.size(), header_size);
   return static_cast<frame_type>(value[1]);
}

[[nodiscard]] std::uint16_t flags_of(const bytes& value) {
   BOOST_REQUIRE_GE(value.size(), header_size);
   return static_cast<std::uint16_t>((static_cast<std::uint16_t>(value[2]) << 8U) | value[3]);
}

[[nodiscard]] std::uint32_t stream_id_of(const bytes& value) {
   return load_u32(value, 4);
}

[[nodiscard]] std::uint32_t length_of(const bytes& value) {
   return load_u32(value, 8);
}

template <typename T>
struct spawned_result {
   explicit spawned_result(boost::asio::any_io_executor executor)
       : timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   boost::asio::steady_timer timer;
   std::optional<T> value;
   std::exception_ptr error;
   bool done = false;
};

template <typename T>
[[nodiscard]] std::shared_ptr<spawned_result<T>> spawn_result(boost::asio::any_io_executor executor,
                                                             boost::asio::awaitable<T> operation) {
   auto state = std::make_shared<spawned_result<T>>(executor);
   boost::asio::co_spawn(
       executor,
       [state, operation = std::move(operation)]() mutable -> boost::asio::awaitable<void> {
          try {
             state->value.emplace(co_await std::move(operation));
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.expires_at(std::chrono::steady_clock::now());
       },
       boost::asio::detached);
   return state;
}

template <typename T>
boost::asio::awaitable<T> take_result(std::shared_ptr<spawned_result<T>> state) {
   auto error = boost::system::error_code{};
   while (!state->done) {
      state->timer.expires_at((std::chrono::steady_clock::time_point::max)());
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->value);
}

template <>
struct spawned_result<void> {
   explicit spawned_result(boost::asio::any_io_executor executor)
       : timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   boost::asio::steady_timer timer;
   std::exception_ptr error;
   bool done = false;
};

template <>
[[nodiscard]] std::shared_ptr<spawned_result<void>> spawn_result(boost::asio::any_io_executor executor,
                                                                boost::asio::awaitable<void> operation) {
   auto state = std::make_shared<spawned_result<void>>(executor);
   boost::asio::co_spawn(
       executor,
       [state, operation = std::move(operation)]() mutable -> boost::asio::awaitable<void> {
          try {
             co_await std::move(operation);
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.expires_at(std::chrono::steady_clock::now());
       },
       boost::asio::detached);
   return state;
}

template <>
boost::asio::awaitable<void> take_result(std::shared_ptr<spawned_result<void>> state) {
   auto error = boost::system::error_code{};
   while (!state->done) {
      state->timer.expires_at((std::chrono::steady_clock::time_point::max)());
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
}

template <typename T>
boost::asio::awaitable<T> take_result_for(std::shared_ptr<spawned_result<T>> state,
                                          std::chrono::milliseconds timeout) {
   auto error = boost::system::error_code{};
   if (!state->done) {
      state->timer.expires_after(timeout);
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
   BOOST_REQUIRE_MESSAGE(state->done, "yamux operation timed out");
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->value);
}

template <>
boost::asio::awaitable<void> take_result_for(std::shared_ptr<spawned_result<void>> state,
                                             std::chrono::milliseconds timeout) {
   auto error = boost::system::error_code{};
   if (!state->done) {
      state->timer.expires_after(timeout);
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
   BOOST_REQUIRE_MESSAGE(state->done, "yamux operation timed out");
   if (state->error) {
      std::rethrow_exception(state->error);
   }
}

struct pipe_state {
   explicit pipe_state(boost::asio::any_io_executor executor)
       : read_timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   std::mutex mutex;
   boost::asio::steady_timer read_timer;
   std::deque<bytes> reads;
   bool closed = false;
   std::uint64_t writes = 0;
};

class pipe_stream final : public fcl::transport::detail::stream_concept {
 public:
   pipe_stream(std::int64_t id, std::shared_ptr<pipe_state> inbound, std::shared_ptr<pipe_state> outbound)
       : id_(id), inbound_(std::move(inbound)), outbound_(std::move(outbound)) {}

   [[nodiscard]] bool valid() const noexcept override {
      auto lock = std::scoped_lock{inbound_->mutex};
      return !inbound_->closed;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> value) override {
      {
         auto lock = std::scoped_lock{outbound_->mutex};
         if (outbound_->closed) {
            FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "pipe stream closed");
         }
         outbound_->reads.push_back(bytes{value.begin(), value.end()});
         ++outbound_->writes;
      }
      outbound_->read_timer.cancel();
      co_return;
   }

   boost::asio::awaitable<bytes> async_read() override {
      auto error = boost::system::error_code{};
      while (true) {
         {
            auto lock = std::scoped_lock{inbound_->mutex};
            if (!inbound_->reads.empty()) {
               auto out = std::move(inbound_->reads.front());
               inbound_->reads.pop_front();
               co_return out;
            }
            if (inbound_->closed) {
               FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "pipe stream closed");
            }
         }
         co_await inbound_->read_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (error && error != boost::asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   boost::asio::awaitable<void> async_close() override {
      {
         auto lock = std::scoped_lock{inbound_->mutex, outbound_->mutex};
         inbound_->closed = true;
         outbound_->closed = true;
      }
      inbound_->read_timer.cancel();
      outbound_->read_timer.cancel();
      co_return;
   }

 private:
   std::int64_t id_ = 0;
   std::shared_ptr<pipe_state> inbound_;
   std::shared_ptr<pipe_state> outbound_;
};

struct stream_pair {
   fcl::transport::stream left;
   fcl::transport::stream right;
};

[[nodiscard]] stream_pair make_stream_pair(boost::asio::any_io_executor executor) {
   auto left_state = std::make_shared<pipe_state>(executor);
   auto right_state = std::make_shared<pipe_state>(executor);
   return stream_pair{
       .left = fcl::transport::detail::stream_access::make(
           std::make_shared<pipe_stream>(1, left_state, right_state)),
       .right = fcl::transport::detail::stream_access::make(
           std::make_shared<pipe_stream>(2, right_state, left_state)),
   };
}

boost::asio::awaitable<void> yamux_open_accept_and_early_data() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto initiator = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator};
   auto responder = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};

   auto accept = spawn_result<fcl::transport::stream>(executor, responder.async_accept_stream());
   auto outbound = co_await initiator.async_open_stream();
   BOOST_CHECK_EQUAL(outbound.id(), 1);

   const auto payload = text_bytes("early request");
   co_await outbound.async_write(payload);
   auto inbound = co_await take_result(accept);
   BOOST_CHECK_EQUAL(inbound.id(), 1);
   auto received = co_await inbound.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received.begin(), received.end(), payload.begin(), payload.end());

   co_await initiator.async_close();
   co_await responder.async_close();
}

boost::asio::awaitable<void> yamux_concurrent_streams_do_not_cross_deliver() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator};
   auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};

   auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
   auto accept_second = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
   auto first = co_await left.async_open_stream();
   auto second = co_await left.async_open_stream();
   BOOST_CHECK_EQUAL(first.id(), 1);
   BOOST_CHECK_EQUAL(second.id(), 3);

   const auto first_payload = text_bytes("stream-one");
   const auto second_payload = text_bytes("stream-two");
   co_await second.async_write(second_payload);
   co_await first.async_write(first_payload);

   auto inbound_first = co_await take_result(accept_first);
   auto inbound_second = co_await take_result(accept_second);
   if (inbound_first.id() > inbound_second.id()) {
      std::swap(inbound_first, inbound_second);
   }
   auto received_first = co_await inbound_first.async_read();
   auto received_second = co_await inbound_second.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_first.begin(), received_first.end(), first_payload.begin(),
                                 first_payload.end());
   BOOST_CHECK_EQUAL_COLLECTIONS(received_second.begin(), received_second.end(), second_payload.begin(),
                                 second_payload.end());

   co_await left.async_close();
   co_await right.async_close();
}

boost::asio::awaitable<void> yamux_flow_control_waits_for_window_update() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto options = fcl::yamux::options{.initial_window = 4, .max_frame_size = 4};
   auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator, options};
   auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder, options};

   auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
   auto outbound = co_await left.async_open_stream();
   auto inbound = co_await take_result(accept);

   const auto payload = text_bytes("abcdefgh");
   auto write = spawn_result<void>(executor, outbound.async_write(payload));
   auto first = co_await inbound.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(first.begin(), first.end(), payload.begin(), payload.begin() + 4);
   auto second = co_await inbound.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(second.begin(), second.end(), payload.begin() + 4, payload.end());
   co_await take_result(write);

   co_await left.async_close();
   co_await right.async_close();
}

boost::asio::awaitable<void> yamux_close_flushes_pending_data_and_read_after_close_fails() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator};
   auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};

   auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
   auto outbound = co_await left.async_open_stream();
   auto inbound = co_await take_result(accept);

   const auto payload = text_bytes("flush-before-close");
   co_await outbound.async_write(payload);
   co_await outbound.async_close();
   auto received = co_await inbound.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received.begin(), received.end(), payload.begin(), payload.end());
   BOOST_CHECK_THROW((void)co_await inbound.async_read(), fcl::yamux::exceptions::closed);

   co_await left.async_close();
   co_await right.async_close();
}

boost::asio::awaitable<void> yamux_limits_and_malformed_frames_are_typed() {
   auto executor = co_await boost::asio::this_coro::executor;
   {
      auto pair = make_stream_pair(executor);
      auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator,
                                      fcl::yamux::options{.max_frame_size = 3}};
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_frame_size = 3}};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      auto outbound = co_await left.async_open_stream();
      auto inbound = co_await take_result(accept);
      const auto payload = text_bytes("four");
      co_await outbound.async_write(payload);
      auto first = co_await inbound.async_read();
      auto second = co_await inbound.async_read();
      BOOST_CHECK_EQUAL_COLLECTIONS(first.begin(), first.end(), payload.begin(), payload.begin() + 3);
      BOOST_CHECK_EQUAL_COLLECTIONS(second.begin(), second.end(), payload.begin() + 3, payload.end());
      co_await left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      const auto payload = text_bytes("early-data");
      co_await pair.left.async_write(frame(frame_type::data, syn, 1, static_cast<std::uint32_t>(payload.size()), payload));
      auto response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(response), 1U);
      BOOST_CHECK_EQUAL(length_of(response), 256U * 1024U);

      auto inbound = co_await take_result(accept);
      BOOST_CHECK_EQUAL(inbound.id(), 1);
      const auto received = co_await inbound.async_read();
      BOOST_TEST(received == payload, boost::test_tools::per_element());
      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 0));
      auto response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(response), 1U);

      auto inbound = co_await take_result(accept);
      const auto payload = text_bytes("initial-window-response");
      auto write = spawn_result<void>(executor, inbound.async_write(payload));
      auto outbound = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(outbound), frame_type::data);
      BOOST_CHECK_EQUAL(stream_id_of(outbound), 1U);
      BOOST_CHECK_EQUAL(length_of(outbound), payload.size());
      co_await take_result_for(write, std::chrono::seconds{1});
      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator};
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_stream_buffer = 3}};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      auto outbound = co_await left.async_open_stream();
      auto inbound = co_await take_result(accept);
      co_await outbound.async_write(text_bytes("four"));
      BOOST_CHECK_THROW((void)co_await inbound.async_read(), fcl::yamux::exceptions::stream_reset);
      co_await left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto malformed = frame(frame_type::data, 0, 1, 0);
      malformed[0] = 1;
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(malformed);
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::protocol_error);
      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, 0, 0, 0));
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::protocol_error);
      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_frame_size = 3}};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, 0, 1, 4));
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::resource_limit);
      co_await pair.left.async_close();
      co_await right.async_close();
   }
}

boost::asio::awaitable<void> yamux_configured_limits_are_behavioral() {
   auto executor = co_await boost::asio::this_coro::executor;
   {
      auto pair = make_stream_pair(executor);
      auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator,
                                      fcl::yamux::options{.max_streams = 1}};
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      auto first = co_await left.async_open_stream();
      auto inbound = co_await take_result(accept);
      BOOST_CHECK_EQUAL(first.id(), 1);
      BOOST_CHECK_EQUAL(inbound.id(), 1);
      BOOST_CHECK_THROW((void)co_await left.async_open_stream(), fcl::yamux::exceptions::resource_limit);
      co_await left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_pending_accepts = 1}};
      auto local_open = spawn_result<fcl::transport::stream>(executor, right.async_open_stream());
      auto local_syn = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(local_syn), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(local_syn), syn);
      BOOST_CHECK_EQUAL(stream_id_of(local_syn), 2U);
      (void)co_await take_result(local_open);

      co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 256 * 1024));
      auto first_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(first_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(first_response), 1U);
      BOOST_CHECK_EQUAL(length_of(first_response), 256U * 1024U);

      co_await pair.left.async_write(frame(frame_type::window_update, syn, 3, 256 * 1024));
      auto second_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(second_response), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(second_response), rst);
      BOOST_CHECK_EQUAL(stream_id_of(second_response), 3U);

      auto accepted = co_await right.async_accept_stream();
      BOOST_CHECK_EQUAL(accepted.id(), 1);
      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator};
      auto right = fcl::yamux::session{
          std::move(pair.right),
          fcl::yamux::side::responder,
          fcl::yamux::options{.max_stream_buffer = 8, .max_session_buffer = 3},
      };
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      auto outbound = co_await left.async_open_stream();
      auto inbound = co_await take_result(accept);
      co_await outbound.async_write(text_bytes("four"));
      BOOST_CHECK_THROW((void)co_await inbound.async_read(), fcl::yamux::exceptions::stream_reset);
      co_await left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto session_options = fcl::yamux::options{.initial_window = 1, .max_stream_window = 2, .max_frame_size = 8};
      auto left = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::initiator, session_options};
      auto outbound = co_await left.async_open_stream();
      auto local_syn = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(local_syn), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(local_syn), syn);
      BOOST_CHECK_EQUAL(stream_id_of(local_syn), 1U);
      BOOST_CHECK_EQUAL(length_of(local_syn), 1U);

      const auto payload = text_bytes("abcd");
      auto write = spawn_result<void>(executor, outbound.async_write(payload));
      auto first = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first), frame_type::data);
      BOOST_CHECK_EQUAL(stream_id_of(first), 1U);
      BOOST_CHECK_EQUAL(length_of(first), 1U);

      co_await pair.left.async_write(frame(frame_type::window_update, 0, 1, 100));
      auto second = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(second), frame_type::data);
      BOOST_CHECK_EQUAL(stream_id_of(second), 1U);
      BOOST_CHECK_EQUAL(length_of(second), 2U);

      co_await pair.left.async_write(frame(frame_type::window_update, 0, 1, 1));
      auto third = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(third), frame_type::data);
      BOOST_CHECK_EQUAL(stream_id_of(third), 1U);
      BOOST_CHECK_EQUAL(length_of(third), 1U);
      co_await take_result(write);
      co_await pair.left.async_close();
      co_await left.async_close();
   }
}

boost::asio::awaitable<void> yamux_control_frames_are_handled() {
   auto executor = co_await boost::asio::this_coro::executor;
   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::ping, 0, 0, 0x01020304));
      auto response = co_await pair.left.async_read();
      BOOST_REQUIRE_EQUAL(response.size(), 12U);
      BOOST_CHECK_EQUAL(response[1], static_cast<std::uint8_t>(frame_type::ping));
      BOOST_CHECK_EQUAL(response[3], 0x02U);
      BOOST_CHECK_EQUAL(response[8], 0x01U);
      BOOST_CHECK_EQUAL(response[9], 0x02U);
      BOOST_CHECK_EQUAL(response[10], 0x03U);
      BOOST_CHECK_EQUAL(response[11], 0x04U);
      right.cancel();
      co_await pair.left.async_close();
      (void)accept;
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::go_away, 0, 0, 0));
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::closed);
      co_await pair.left.async_close();
      co_await right.async_close();
   }
}

boost::asio::awaitable<void> yamux_transport_session_wrapper_delegates() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto left = fcl::yamux::make_session(std::move(pair.left), fcl::yamux::side::initiator);
   auto right = fcl::yamux::make_session(std::move(pair.right), fcl::yamux::side::responder);

   auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
   auto outbound = co_await left.async_open_stream();
   auto inbound = co_await take_result(accept);
   const auto payload = text_bytes("transport session");
   co_await outbound.async_write(payload);
   auto received = co_await inbound.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received.begin(), received.end(), payload.begin(), payload.end());

   co_await left.async_close();
   co_await right.async_close();
}

} // namespace

BOOST_AUTO_TEST_SUITE(yamux)

BOOST_AUTO_TEST_CASE(yamux_supports_open_accept_and_early_data) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_open_accept_and_early_data());
}

BOOST_AUTO_TEST_CASE(yamux_keeps_concurrent_stream_payloads_isolated) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_concurrent_streams_do_not_cross_deliver());
}

BOOST_AUTO_TEST_CASE(yamux_applies_flow_control_with_window_updates) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_flow_control_waits_for_window_update());
}

BOOST_AUTO_TEST_CASE(yamux_close_flushes_and_read_after_close_is_rejected) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_close_flushes_pending_data_and_read_after_close_fails());
}

BOOST_AUTO_TEST_CASE(yamux_rejects_limits_and_malformed_frames_with_typed_errors) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_limits_and_malformed_frames_are_typed());
}

BOOST_AUTO_TEST_CASE(yamux_enforces_configured_runtime_limits) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_configured_limits_are_behavioral());
}

BOOST_AUTO_TEST_CASE(yamux_handles_ping_and_goaway_control_frames) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_control_frames_are_handled());
}

BOOST_AUTO_TEST_CASE(yamux_exposes_transport_session_wrapper) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_transport_session_wrapper_delegates());
}

BOOST_AUTO_TEST_SUITE_END()
