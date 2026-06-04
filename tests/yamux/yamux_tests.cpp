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
import fcl.exceptions;
import fcl.transport.buffer;
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
inline constexpr std::uint16_t fin = 0x04;
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

[[nodiscard]] bytes payload_of(const bytes& value) {
   BOOST_REQUIRE_GE(value.size(), header_size);
   return bytes{value.begin() + static_cast<std::ptrdiff_t>(header_size), value.end()};
}

void append_bytes(bytes& target, const bytes& source) {
   target.insert(target.end(), source.begin(), source.end());
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

boost::asio::awaitable<void> close_transport_for_test(fcl::transport::stream& stream) {
   try {
      co_await stream.async_close();
   } catch (const fcl::transport::exceptions::closed&) {
   }
}

boost::asio::awaitable<bytes> read_transport_for_test(fcl::transport::stream& stream, std::string_view label,
                                                       std::chrono::milliseconds timeout = std::chrono::seconds{1}) {
   auto executor = co_await boost::asio::this_coro::executor;
   auto state = spawn_result<bytes>(executor, stream.async_read());
   auto error = boost::system::error_code{};
   if (!state->done) {
      state->timer.expires_after(timeout);
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
   BOOST_REQUIRE_MESSAGE(state->done, "yamux operation timed out while waiting for " << label);
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->value);
}

struct pending_write {
   pending_write(boost::asio::any_io_executor executor, bytes write_value)
       : timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()),
         value(std::move(write_value)) {}

   boost::asio::steady_timer timer;
   bytes value;
   bool released = false;
};

struct pipe_state {
   explicit pipe_state(boost::asio::any_io_executor executor)
       : read_timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   std::mutex mutex;
   boost::asio::steady_timer read_timer;
   std::deque<bytes> reads;
   std::deque<std::shared_ptr<pending_write>> pending_writes;
   bool closed = false;
   bool hold_writes = false;
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
      auto executor = co_await boost::asio::this_coro::executor;
      auto pending = std::shared_ptr<pending_write>{};
      {
         auto lock = std::scoped_lock{outbound_->mutex};
         if (outbound_->closed) {
            FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "pipe stream closed");
         }
         auto owned = bytes{value.begin(), value.end()};
         if (outbound_->hold_writes) {
            pending = std::make_shared<pending_write>(executor, std::move(owned));
            outbound_->pending_writes.push_back(pending);
         } else {
            outbound_->reads.push_back(std::move(owned));
            ++outbound_->writes;
         }
      }
      outbound_->read_timer.cancel();
      if (!pending) {
         co_return;
      }

      auto error = boost::system::error_code{};
      while (true) {
         {
            auto lock = std::scoped_lock{outbound_->mutex};
            if (pending->released) {
               outbound_->reads.push_back(std::move(pending->value));
               ++outbound_->writes;
               break;
            }
            if (outbound_->closed) {
               FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "pipe stream closed");
            }
         }
         co_await pending->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (error && error != boost::asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
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
         for (const auto& pending : inbound_->pending_writes) {
            pending->timer.cancel();
         }
         for (const auto& pending : outbound_->pending_writes) {
            pending->timer.cancel();
         }
         inbound_->pending_writes.clear();
         outbound_->pending_writes.clear();
      }
      inbound_->read_timer.cancel();
      outbound_->read_timer.cancel();
      co_return;
   }

   void cancel() override {
      {
         auto lock = std::scoped_lock{inbound_->mutex, outbound_->mutex};
         inbound_->closed = true;
         outbound_->closed = true;
         for (const auto& pending : inbound_->pending_writes) {
            pending->timer.cancel();
         }
         for (const auto& pending : outbound_->pending_writes) {
            pending->timer.cancel();
         }
         inbound_->pending_writes.clear();
         outbound_->pending_writes.clear();
      }
      inbound_->read_timer.cancel();
      outbound_->read_timer.cancel();
   }

 private:
   std::int64_t id_ = 0;
   std::shared_ptr<pipe_state> inbound_;
   std::shared_ptr<pipe_state> outbound_;
};

struct stream_pair {
   fcl::transport::stream left;
   fcl::transport::stream right;
   std::shared_ptr<pipe_state> left_state;
   std::shared_ptr<pipe_state> right_state;
};

[[nodiscard]] stream_pair make_stream_pair(boost::asio::any_io_executor executor) {
   auto left_state = std::make_shared<pipe_state>(executor);
   auto right_state = std::make_shared<pipe_state>(executor);
   return stream_pair{
       .left = fcl::transport::detail::stream_access::make(
           std::make_shared<pipe_stream>(1, left_state, right_state)),
       .right = fcl::transport::detail::stream_access::make(
           std::make_shared<pipe_stream>(2, right_state, left_state)),
       .left_state = left_state,
       .right_state = right_state,
   };
}

void hold_writes(const std::shared_ptr<pipe_state>& state, bool value) {
   auto lock = std::scoped_lock{state->mutex};
   state->hold_writes = value;
}

void release_next_write(const std::shared_ptr<pipe_state>& state) {
   auto pending = std::shared_ptr<pending_write>{};
   {
      auto lock = std::scoped_lock{state->mutex};
      BOOST_REQUIRE(!state->pending_writes.empty());
      pending = state->pending_writes.front();
      state->pending_writes.pop_front();
      pending->released = true;
   }
   pending->timer.cancel();
}

boost::asio::awaitable<void> wait_for_pending_writes(const std::shared_ptr<pipe_state>& state,
                                                     std::size_t expected) {
   auto error = boost::system::error_code{};
   while (true) {
      {
         auto lock = std::scoped_lock{state->mutex};
         if (state->pending_writes.size() >= expected) {
            co_return;
         }
      }
      co_await state->read_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
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

   const auto chunk_payload = text_bytes("chunk response");
   co_await inbound.async_write(fcl::transport::chunk{chunk_payload});
   auto received_chunk = co_await outbound.async_read_chunk();
   const auto received_chunk_bytes = received_chunk.to_vector();
   BOOST_CHECK_EQUAL_COLLECTIONS(
       received_chunk_bytes.begin(), received_chunk_bytes.end(), chunk_payload.begin(), chunk_payload.end());

   const auto framed_chunk = text_bytes("framed chunk over yamux");
   co_await outbound.async_write_frame(fcl::transport::chunk{framed_chunk});
   auto received_frame_chunk = co_await inbound.async_read_frame_chunk();
   const auto received_frame_chunk_bytes = received_frame_chunk.to_vector();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_frame_chunk_bytes.begin(), received_frame_chunk_bytes.end(),
                                 framed_chunk.begin(), framed_chunk.end());

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

boost::asio::awaitable<void> yamux_concurrent_writes_are_fifo_without_timer_spin() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto options = fcl::yamux::options{.initial_window = 64, .max_frame_size = 64};
   auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator, options};

   auto first = co_await left.async_open_stream();
   auto first_syn = co_await pair.right.async_read();
   BOOST_CHECK_EQUAL(type_of(first_syn), frame_type::window_update);
   BOOST_CHECK_EQUAL(stream_id_of(first_syn), 1U);
   auto second = co_await left.async_open_stream();
   auto second_syn = co_await pair.right.async_read();
   BOOST_CHECK_EQUAL(type_of(second_syn), frame_type::window_update);
   BOOST_CHECK_EQUAL(stream_id_of(second_syn), 3U);
   auto third = co_await left.async_open_stream();
   auto third_syn = co_await pair.right.async_read();
   BOOST_CHECK_EQUAL(type_of(third_syn), frame_type::window_update);
   BOOST_CHECK_EQUAL(stream_id_of(third_syn), 5U);

   hold_writes(pair.right_state, true);
   const auto first_payload = text_bytes("first");
   const auto second_payload = text_bytes("second");
   const auto third_payload = text_bytes("third");
   auto first_write = spawn_result<void>(executor, first.async_write(first_payload));
   auto second_write = spawn_result<void>(executor, second.async_write(second_payload));
   auto third_write = spawn_result<void>(executor, third.async_write(third_payload));

   co_await wait_for_pending_writes(pair.right_state, 1);
   release_next_write(pair.right_state);
   auto first_frame = co_await pair.right.async_read();
   BOOST_CHECK_EQUAL(type_of(first_frame), frame_type::data);
   BOOST_CHECK_EQUAL(stream_id_of(first_frame), 1U);
   BOOST_TEST(payload_of(first_frame) == first_payload, boost::test_tools::per_element());
   co_await take_result_for(first_write, std::chrono::seconds{1});

   co_await wait_for_pending_writes(pair.right_state, 1);
   release_next_write(pair.right_state);
   auto second_frame = co_await pair.right.async_read();
   BOOST_CHECK_EQUAL(type_of(second_frame), frame_type::data);
   BOOST_CHECK_EQUAL(stream_id_of(second_frame), 3U);
   BOOST_TEST(payload_of(second_frame) == second_payload, boost::test_tools::per_element());
   co_await take_result_for(second_write, std::chrono::seconds{1});

   co_await wait_for_pending_writes(pair.right_state, 1);
   release_next_write(pair.right_state);
   auto third_frame = co_await pair.right.async_read();
   BOOST_CHECK_EQUAL(type_of(third_frame), frame_type::data);
   BOOST_CHECK_EQUAL(stream_id_of(third_frame), 5U);
   BOOST_TEST(payload_of(third_frame) == third_payload, boost::test_tools::per_element());
   co_await take_result_for(third_write, std::chrono::seconds{1});

   co_await pair.right.async_close();
   co_await left.async_close();
}

boost::asio::awaitable<void> yamux_cancel_wakes_pending_write_waiters() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto options = fcl::yamux::options{.initial_window = 64, .max_frame_size = 64};
   auto left = fcl::yamux::session{std::move(pair.left), fcl::yamux::side::initiator, options};

   auto first = co_await left.async_open_stream();
   (void)co_await pair.right.async_read();
   auto second = co_await left.async_open_stream();
   (void)co_await pair.right.async_read();

   hold_writes(pair.right_state, true);
   auto first_write = spawn_result<void>(executor, first.async_write(text_bytes("held")));
   auto second_write = spawn_result<void>(executor, second.async_write(text_bytes("waiting")));
   co_await wait_for_pending_writes(pair.right_state, 1);

   left.cancel();

   BOOST_CHECK_THROW((void)co_await take_result_for(first_write, std::chrono::seconds{1}),
                     fcl::exceptions::base);
   BOOST_CHECK_THROW((void)co_await take_result_for(second_write, std::chrono::seconds{1}),
                     fcl::exceptions::base);
   co_await pair.right.async_close();
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
      BOOST_CHECK_THROW(
          (fcl::yamux::session{
              std::move(pair.right),
              fcl::yamux::side::responder,
              fcl::yamux::options{
                  .initial_window = 8,
                  .max_stream_window = 8,
                  .max_stream_buffer = 7,
              },
          }),
          fcl::yamux::exceptions::invalid_options);
      co_await pair.left.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      BOOST_CHECK_THROW(
          (fcl::yamux::session{
              std::move(pair.right),
              fcl::yamux::side::responder,
              fcl::yamux::options{
                  .initial_window = 8,
                  .max_stream_window = 8,
                  .max_stream_buffer = 8,
                  .max_session_buffer = 7,
              },
          }),
          fcl::yamux::exceptions::invalid_options);
      co_await pair.left.async_close();
   }

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
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{
                                           .initial_window = 3,
                                           .max_stream_window = 3,
                                           .max_frame_size = 8,
                                           .max_stream_buffer = 3,
                                           .max_session_buffer = 8,
                                       }};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, syn, 1, 4, text_bytes("four")));
      auto ack_frame = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(ack_frame), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(ack_frame), ack);
      BOOST_CHECK_EQUAL(stream_id_of(ack_frame), 1U);
      auto reset_frame = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(reset_frame), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(reset_frame), rst);
      BOOST_CHECK_EQUAL(stream_id_of(reset_frame), 1U);
      auto inbound = co_await take_result(accept);
      BOOST_CHECK_THROW((void)co_await inbound.async_read(), fcl::yamux::exceptions::stream_reset);
      co_await close_transport_for_test(pair.left);
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto malformed = frame(frame_type::data, 0, 1, 0);
      malformed[0] = 1;
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(malformed);
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::protocol_error);
      co_await close_transport_for_test(pair.left);
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, 0, 0, 0));
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::protocol_error);
      co_await close_transport_for_test(pair.left);
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_frame_size = 3}};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, 0, 1, 4));
      BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::yamux::exceptions::resource_limit);
      co_await close_transport_for_test(pair.left);
   }
}

boost::asio::awaitable<void> yamux_resource_overflow_resets_only_offending_stream() {
   auto executor = co_await boost::asio::this_coro::executor;
   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{
          std::move(pair.right),
          fcl::yamux::side::responder,
          fcl::yamux::options{
              .initial_window = 4,
              .max_stream_window = 4,
              .max_frame_size = 16,
              .max_stream_buffer = 4,
              .max_session_buffer = 16,
          },
      };
      auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, syn, 1, 5, text_bytes("abcde")));
      BOOST_TEST_CHECKPOINT("stream buffer overflow: waiting for ACK");
      auto first_response = co_await read_transport_for_test(pair.left, "stream-buffer overflow ACK");
      BOOST_CHECK_EQUAL(type_of(first_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(first_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(first_response), 1U);
      BOOST_TEST_CHECKPOINT("stream buffer overflow: waiting for RST");
      auto first_reset = co_await read_transport_for_test(pair.left, "stream-buffer overflow RST");
      BOOST_CHECK_EQUAL(type_of(first_reset), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(first_reset), rst);
      BOOST_CHECK_EQUAL(stream_id_of(first_reset), 1U);
      auto first = co_await take_result(accept_first);
      BOOST_CHECK_THROW((void)co_await first.async_read(), fcl::yamux::exceptions::stream_reset);

      auto accept_second = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      const auto payload = text_bytes("ok");
      co_await pair.left.async_write(frame(frame_type::data, syn, 3, static_cast<std::uint32_t>(payload.size()), payload));
      BOOST_TEST_CHECKPOINT("stream buffer overflow: waiting for second stream ACK");
      auto second_response = co_await read_transport_for_test(pair.left, "stream-buffer second ACK");
      BOOST_CHECK_EQUAL(type_of(second_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(second_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(second_response), 3U);
      auto second = co_await take_result_for(accept_second, std::chrono::seconds{1});
      auto received = co_await second.async_read();
      BOOST_TEST(received == payload, boost::test_tools::per_element());

      co_await close_transport_for_test(pair.left);
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{
          std::move(pair.right),
          fcl::yamux::side::responder,
          fcl::yamux::options{
              .initial_window = 4,
              .max_stream_window = 4,
              .max_frame_size = 16,
              .max_stream_buffer = 8,
              .max_session_buffer = 4,
          },
      };
      auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, syn, 1, 4, text_bytes("hold")));
      BOOST_TEST_CHECKPOINT("session buffer overflow: waiting for first ACK");
      auto first_response = co_await read_transport_for_test(pair.left, "session-buffer first ACK");
      BOOST_CHECK_EQUAL(type_of(first_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(first_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(first_response), 1U);
      co_await pair.left.async_write(frame(frame_type::data, syn, 3, 1, text_bytes("x")));
      BOOST_TEST_CHECKPOINT("session buffer overflow: waiting for second ACK");
      auto second_response = co_await read_transport_for_test(pair.left, "session-buffer second ACK");
      BOOST_CHECK_EQUAL(type_of(second_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(second_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(second_response), 3U);
      BOOST_TEST_CHECKPOINT("session buffer overflow: waiting for second RST");
      auto second_reset = co_await read_transport_for_test(pair.left, "session-buffer second RST");
      BOOST_CHECK_EQUAL(type_of(second_reset), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(second_reset), rst);
      BOOST_CHECK_EQUAL(stream_id_of(second_reset), 3U);

      auto first = co_await take_result_for(accept_first, std::chrono::seconds{1});
      auto second = co_await right.async_accept_stream();
      BOOST_CHECK_EQUAL(first.id(), 1);
      BOOST_CHECK_EQUAL(second.id(), 3);
      BOOST_CHECK_THROW((void)co_await second.async_read(), fcl::yamux::exceptions::stream_reset);
      auto received = co_await first.async_read();
      BOOST_TEST(received == text_bytes("hold"), boost::test_tools::per_element());
      auto first_window = co_await read_transport_for_test(pair.left, "session-buffer first WINDOW_UPDATE");
      BOOST_CHECK_EQUAL(type_of(first_window), frame_type::window_update);
      BOOST_CHECK_EQUAL(stream_id_of(first_window), 1U);
      BOOST_CHECK_EQUAL(length_of(first_window), 4U);

      auto accept_third = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      const auto payload = text_bytes("next");
      co_await pair.left.async_write(frame(frame_type::data, syn, 5, static_cast<std::uint32_t>(payload.size()), payload));
      BOOST_TEST_CHECKPOINT("session buffer overflow: waiting for third ACK");
      auto third_response = co_await read_transport_for_test(pair.left, "session-buffer third ACK");
      BOOST_CHECK_EQUAL(type_of(third_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(third_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(third_response), 5U);
      auto third = co_await take_result_for(accept_third, std::chrono::seconds{1});
      auto third_received = co_await third.async_read();
      BOOST_TEST(third_received == payload, boost::test_tools::per_element());

      co_await close_transport_for_test(pair.left);
   }
}

boost::asio::awaitable<void> yamux_parser_handles_partial_and_buffered_frames() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
   auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());

   const auto first_payload = text_bytes("one");
   const auto second_payload = text_bytes("two");
   auto first_frame = frame(frame_type::data, syn, 1, static_cast<std::uint32_t>(first_payload.size()), first_payload);
   auto second_frame = frame(frame_type::data, syn, 3, static_cast<std::uint32_t>(second_payload.size()), second_payload);
   auto prefix = bytes{first_frame.begin(), first_frame.begin() + 5};
   auto remainder = bytes{first_frame.begin() + 5, first_frame.end()};
   append_bytes(remainder, second_frame);

   co_await pair.left.async_write(prefix);
   co_await pair.left.async_write(remainder);

   auto first_ack = co_await read_transport_for_test(pair.left, "parser first ACK");
   BOOST_CHECK_EQUAL(type_of(first_ack), frame_type::window_update);
   BOOST_CHECK_EQUAL(flags_of(first_ack), ack);
   BOOST_CHECK_EQUAL(stream_id_of(first_ack), 1U);
   auto second_ack = co_await read_transport_for_test(pair.left, "parser second ACK");
   BOOST_CHECK_EQUAL(type_of(second_ack), frame_type::window_update);
   BOOST_CHECK_EQUAL(flags_of(second_ack), ack);
   BOOST_CHECK_EQUAL(stream_id_of(second_ack), 3U);

   auto first = co_await take_result_for(accept_first, std::chrono::seconds{1});
   auto second = co_await right.async_accept_stream();
   BOOST_CHECK_EQUAL(first.id(), 1);
   BOOST_CHECK_EQUAL(second.id(), 3);
   auto first_received = co_await first.async_read();
   auto second_received = co_await second.async_read();
   BOOST_TEST(first_received == first_payload, boost::test_tools::per_element());
   BOOST_TEST(second_received == second_payload, boost::test_tools::per_element());

   co_await pair.left.async_close();
   co_await right.async_close();
}

boost::asio::awaitable<void> yamux_reset_reclaim_releases_buffer_budget_for_open_streams() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pair = make_stream_pair(executor);
   auto right = fcl::yamux::session{
       std::move(pair.right),
       fcl::yamux::side::responder,
       fcl::yamux::options{
           .initial_window = 4,
           .max_stream_window = 4,
           .max_frame_size = 16,
           .max_streams = 2,
           .max_pending_accepts = 2,
           .max_stream_buffer = 4,
           .max_session_buffer = 4,
       },
   };
   auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
   co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 4));
   auto first_ack = co_await read_transport_for_test(pair.left, "reclaim first ACK");
   BOOST_CHECK_EQUAL(type_of(first_ack), frame_type::window_update);
   BOOST_CHECK_EQUAL(flags_of(first_ack), ack);
   BOOST_CHECK_EQUAL(stream_id_of(first_ack), 1U);
   co_await pair.left.async_write(frame(frame_type::window_update, syn, 3, 4));
   auto second_ack = co_await read_transport_for_test(pair.left, "reclaim second ACK");
   BOOST_CHECK_EQUAL(type_of(second_ack), frame_type::window_update);
   BOOST_CHECK_EQUAL(flags_of(second_ack), ack);
   BOOST_CHECK_EQUAL(stream_id_of(second_ack), 3U);
   auto first = co_await take_result_for(accept_first, std::chrono::seconds{1});
   auto second = co_await right.async_accept_stream();
   BOOST_CHECK_EQUAL(first.id(), 1);
   BOOST_CHECK_EQUAL(second.id(), 3);

   co_await pair.left.async_write(frame(frame_type::data, 0, 1, 4, text_bytes("hold")));
   co_await pair.left.async_write(frame(frame_type::data, rst, 1, 0));
   BOOST_CHECK_THROW((void)co_await first.async_read(), fcl::yamux::exceptions::stream_reset);

   const auto payload = text_bytes("pass");
   co_await pair.left.async_write(frame(frame_type::data, 0, 3, static_cast<std::uint32_t>(payload.size()), payload));
   auto received = co_await second.async_read();
   BOOST_TEST(received == payload, boost::test_tools::per_element());

   co_await close_transport_for_test(pair.left);
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
      auto right = fcl::yamux::session{
          std::move(pair.right),
          fcl::yamux::side::responder,
          fcl::yamux::options{
              .initial_window = 3,
              .max_stream_window = 3,
              .max_frame_size = 8,
              .max_stream_buffer = 8,
              .max_session_buffer = 3,
          },
      };
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::data, syn, 1, 4, text_bytes("four")));
      auto ack_frame = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(ack_frame), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(ack_frame), ack);
      BOOST_CHECK_EQUAL(stream_id_of(ack_frame), 1U);
      auto reset_frame = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(reset_frame), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(reset_frame), rst);
      BOOST_CHECK_EQUAL(stream_id_of(reset_frame), 1U);
      auto inbound = co_await take_result(accept);
      BOOST_CHECK_THROW((void)co_await inbound.async_read(), fcl::yamux::exceptions::stream_reset);
      co_await close_transport_for_test(pair.left);
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder};
      auto accept = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 1));
      auto response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(response), 1U);

      auto inbound = co_await take_result(accept);
      auto write = spawn_result<void>(executor, inbound.async_write(text_bytes("ab")));
      auto first = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first), frame_type::data);
      BOOST_CHECK_EQUAL(stream_id_of(first), 1U);
      BOOST_CHECK_EQUAL(length_of(first), 1U);

      if (length_of(first) == 1U) {
         BOOST_CHECK(!write->done);
         co_await pair.left.async_write(frame(frame_type::window_update, 0, 1, 1));
         auto second = co_await pair.left.async_read();
         BOOST_CHECK_EQUAL(type_of(second), frame_type::data);
         BOOST_CHECK_EQUAL(stream_id_of(second), 1U);
         BOOST_CHECK_EQUAL(length_of(second), 1U);
      }
      co_await take_result_for(write, std::chrono::seconds{1});
      co_await pair.left.async_close();
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

boost::asio::awaitable<void> yamux_reclaims_terminal_streams_before_enforcing_stream_cap() {
   auto executor = co_await boost::asio::this_coro::executor;
   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_streams = 1}};
      auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 256 * 1024));
      auto first_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(first_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(first_response), 1U);
      auto first = co_await take_result(accept_first);

      co_await pair.left.async_write(frame(frame_type::data, fin, 1, 0));
      co_await first.async_close();
      auto first_fin = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first_fin), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(first_fin), fin);
      BOOST_CHECK_EQUAL(stream_id_of(first_fin), 1U);

      auto accept_second = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 3, 256 * 1024));
      auto second_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(second_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(second_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(second_response), 3U);
      auto second = co_await take_result_for(accept_second, std::chrono::seconds{1});
      BOOST_CHECK_EQUAL(second.id(), 3);

      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_streams = 1}};
      auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 256 * 1024));
      auto first_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(first_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(first_response), 1U);
      auto first = co_await take_result(accept_first);
      BOOST_CHECK_EQUAL(first.id(), 1);

      co_await pair.left.async_write(frame(frame_type::data, rst, 1, 0));

      auto accept_second = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 3, 256 * 1024));
      auto second_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(second_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(second_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(second_response), 3U);
      auto second = co_await take_result_for(accept_second, std::chrono::seconds{1});
      BOOST_CHECK_EQUAL(second.id(), 3);

      co_await pair.left.async_close();
      co_await right.async_close();
   }

   {
      auto pair = make_stream_pair(executor);
      auto right = fcl::yamux::session{std::move(pair.right), fcl::yamux::side::responder,
                                       fcl::yamux::options{.max_streams = 1}};
      auto accept_first = spawn_result<fcl::transport::stream>(executor, right.async_accept_stream());
      co_await pair.left.async_write(frame(frame_type::window_update, syn, 1, 256 * 1024));
      auto first_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(first_response), frame_type::window_update);
      BOOST_CHECK_EQUAL(flags_of(first_response), ack);
      BOOST_CHECK_EQUAL(stream_id_of(first_response), 1U);
      auto first = co_await take_result(accept_first);
      BOOST_CHECK_EQUAL(first.id(), 1);

      co_await pair.left.async_write(frame(frame_type::window_update, syn, 3, 256 * 1024));
      auto second_response = co_await pair.left.async_read();
      BOOST_CHECK_EQUAL(type_of(second_response), frame_type::data);
      BOOST_CHECK_EQUAL(flags_of(second_response), rst);
      BOOST_CHECK_EQUAL(stream_id_of(second_response), 3U);

      co_await pair.left.async_close();
      co_await right.async_close();
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

BOOST_AUTO_TEST_CASE(yamux_serializes_concurrent_writes_without_starving_waiters) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_concurrent_writes_are_fifo_without_timer_spin());
}

BOOST_AUTO_TEST_CASE(yamux_cancel_unblocks_pending_write_waiters) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_cancel_wakes_pending_write_waiters());
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

BOOST_AUTO_TEST_CASE(yamux_resets_only_streams_that_exceed_buffers) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_resource_overflow_resets_only_offending_stream());
}

BOOST_AUTO_TEST_CASE(yamux_parser_preserves_partial_and_buffered_frames) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_parser_handles_partial_and_buffered_frames());
}

BOOST_AUTO_TEST_CASE(yamux_reset_reclaim_releases_buffer_budget) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_reset_reclaim_releases_buffer_budget_for_open_streams());
}

BOOST_AUTO_TEST_CASE(yamux_enforces_configured_runtime_limits) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_configured_limits_are_behavioral());
}

BOOST_AUTO_TEST_CASE(yamux_reclaims_terminal_streams_before_stream_cap) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, yamux_reclaims_terminal_streams_before_enforcing_stream_cap());
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
