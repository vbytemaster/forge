module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/error_code.hpp>

module forge.net.yamux.session;

import forge.asio.gate;
import forge.asio.notification;
import forge.net.transport.exceptions;

#include "details/session_impl.hxx"
#include "details/session_impl_stream_state.hxx"

namespace forge::net::yamux {
namespace {

inline constexpr std::size_t read_compact_threshold = 65'536;

} // namespace

boost::asio::awaitable<void> session::impl::read_loop() {
   auto terminal = exceptions::code::closed;
   auto message = "yamux read loop stopped";
   auto go_away = std::optional<std::uint32_t>{};
   try {
      auto buffer = detail::bytes{};
      auto consumed = std::size_t{0};
      while (true) {
         const auto next = co_await read_frame(buffer, consumed);
         co_await handle_frame(next.first, next.second);
      }
   } catch (const exceptions::resource_limit&) {
      terminal = exceptions::code::resource_limit;
      message = "yamux resource limit exceeded";
      go_away = detail::go_away_internal;
   } catch (const exceptions::protocol_error&) {
      terminal = exceptions::code::protocol_error;
      message = "yamux protocol error";
      go_away = detail::go_away_protocol;
   } catch (const transport::exceptions::closed&) {
      terminal = exceptions::code::closed;
      message = "yamux underlying stream closed";
   } catch (...) {
      terminal = exceptions::code::closed;
      message = "yamux read loop stopped";
   }
   fail_session(terminal, message);
   request_stream_cancel_loop_stop();
   const auto cancel_deadline = deadline_after(options_.close_timeout);
   auto reset_writer_drained = false;
   try {
      reset_writer_drained = co_await wait_for_stream_cancel_loop_until(cancel_deadline);
   } catch (...) {
      (void)cancel_transport_noexcept();
   }
   if (!reset_writer_drained) {
      (void)cancel_transport_noexcept();
      try {
         co_await wait_for_stream_cancel_loop();
      } catch (...) {
         // The reset writer owns the session until its completion handler
         // publishes done. Transport cancellation still guarantees progress.
      }
   }
   const auto owns_close = reset_writer_drained && go_away && start_close(*go_away);
   if (owns_close) {
      try {
         co_await async_send_terminal_go_away(*go_away);
      } catch (...) {
         (void)cancel_transport_noexcept();
      }
   }
   finish_read_loop();
   if (owns_close) {
      finish_close();
   }
}

boost::asio::awaitable<void> session::impl::async_send_terminal_go_away(std::uint32_t code) {
   auto executor = co_await boost::asio::this_coro::executor;
   const auto deadline = deadline_after(options_.close_timeout);
   transport_writes_.seal();
   if (!co_await transport_writes_.async_wait_until(deadline)) {
      (void)cancel_transport_noexcept();
      try {
         co_await stream_.async_close();
      } catch (...) {
      }
      co_return;
   }

   auto deadline_timer = boost::asio::steady_timer{executor, deadline};
   auto weak = weak_from_this();
   deadline_timer.async_wait([weak](const boost::system::error_code& error) {
      if (!error) {
         if (auto self = weak.lock()) {
            (void)self->cancel_transport_noexcept();
         }
      }
   });

   try {
      auto outbound = transport::chunk{detail::encode_frame(detail::frame_type::go_away, 0, 0, code)};
      co_await stream_.async_write(std::move(outbound));
   } catch (...) {
      // The original typed protocol/resource failure is already terminal.
   }
   try {
      co_await stream_.async_close();
   } catch (...) {
      // Transport teardown is best-effort and never replaces the protocol cause.
   }
   try {
      deadline_timer.cancel();
   } catch (...) {
   }
}

void session::impl::compact_read_buffer(detail::bytes& buffer, std::size_t& consumed) {
   if (consumed == 0) {
      return;
   }
   if (consumed >= buffer.size()) {
      buffer.clear();
      consumed = 0;
      return;
   }
   auto compacted = detail::bytes{};
   compacted.reserve(buffer.size() - consumed);
   compacted.insert(compacted.end(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed), buffer.end());
   buffer = std::move(compacted);
   consumed = 0;
}

boost::asio::awaitable<std::pair<detail::frame_header, detail::bytes>>
session::impl::read_frame(detail::bytes& buffer, std::size_t& consumed) {
   while (buffer.size() - consumed < detail::header_size) {
      compact_read_buffer(buffer, consumed);
      auto chunk = co_await stream_.async_read();
      if (chunk.empty()) {
         continue;
      }
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }

   auto view = std::span<const std::uint8_t>{buffer.data() + consumed, buffer.size() - consumed};
   if (view[0] != detail::version) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux frame version mismatch");
   }
   if (view[1] > static_cast<std::uint8_t>(detail::frame_type::go_away)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux frame type is invalid");
   }
   const auto flags = static_cast<std::uint16_t>((static_cast<std::uint16_t>(view[2]) << 8U) | view[3]);
   if ((flags & ~detail::known_flags) != 0U) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux frame has unknown flags");
   }

   auto header = detail::frame_header{
       .type = static_cast<detail::frame_type>(view[1]),
       .flags = flags,
       .stream_id = detail::load_u32(view, 4),
       .length = detail::load_u32(view, 8),
   };

   if (header.type == detail::frame_type::data && header.length > options_.max_frame_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_limit, "yamux frame exceeds maximum size");
   }
   const auto payload_size = header.type == detail::frame_type::data ? static_cast<std::size_t>(header.length) : 0U;
   while (buffer.size() - consumed < detail::header_size + payload_size) {
      compact_read_buffer(buffer, consumed);
      auto chunk = co_await stream_.async_read();
      if (chunk.empty()) {
         continue;
      }
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }

   auto payload = detail::bytes{};
   if (payload_size > 0) {
      const auto payload_begin = consumed + detail::header_size;
      const auto payload_end = payload_begin + payload_size;
      payload.insert(payload.end(), buffer.begin() + static_cast<std::ptrdiff_t>(payload_begin),
                     buffer.begin() + static_cast<std::ptrdiff_t>(payload_end));
   }
   consumed += detail::header_size + payload_size;
   if (consumed >= buffer.size() || consumed > read_compact_threshold) {
      compact_read_buffer(buffer, consumed);
   }
   co_return std::pair{header, std::move(payload)};
}

boost::asio::awaitable<void> session::impl::handle_frame(const detail::frame_header& header,
                                                         const detail::bytes& payload) {
   switch (header.type) {
   case detail::frame_type::data:
      co_await handle_data(header, payload);
      co_return;
   case detail::frame_type::window_update:
      co_await handle_window_update(header);
      co_return;
   case detail::frame_type::ping:
      co_await handle_ping(header);
      co_return;
   case detail::frame_type::go_away:
      handle_go_away(header);
      co_return;
   }
   FORGE_THROW_EXCEPTION(exceptions::protocol_error, "unknown yamux frame type");
}

boost::asio::awaitable<void> session::impl::handle_data(const detail::frame_header& header,
                                                        const detail::bytes& payload) {
   if (header.stream_id == 0 || header.length != payload.size()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "invalid yamux data frame");
   }

   if ((header.flags & detail::rst) != 0U) {
      auto lock = std::scoped_lock{mutex_};
      if (const auto found = streams_.find(header.stream_id);
          found != streams_.end() && !found->second->reset &&
          !(found->second->local_fin && found->second->remote_fin)) {
         reset_stream_locked(found->second);
      }
      co_return;
   }

   if ((header.flags & detail::syn) != 0U && payload.size() > detail::initial_stream_window) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux DATA+SYN exceeds receive window");
   }

   auto opened = std::shared_ptr<stream_state>{};
   if ((header.flags & detail::syn) != 0U) {
      opened = co_await handle_stream_open(header, detail::initial_stream_window);
      if (!opened) {
         co_return;
      }
   }

   auto state = opened;
   if (!state) {
      auto lock = std::scoped_lock{mutex_};
      if (const auto found = streams_.find(header.stream_id); found != streams_.end()) {
         state = found->second;
      } else {
         co_return;
      }
   }

   auto needs_reset = false;
   while (true) {
      const auto observed = state->receive_credit_notification.epoch();
      auto wait_for_credit_send = false;
      auto over_credit = false;
      {
         auto lock = std::scoped_lock{mutex_};
         if (state->reset || (state->local_fin && state->remote_fin)) {
            co_return;
         }
         if (!payload.empty() && payload.size() > state->receive_window) {
            const auto potential_window =
                static_cast<std::uint64_t>(state->receive_window) + state->pending_receive_credit;
            if (state->pending_receive_credit > 0 && payload.size() <= potential_window) {
               wait_for_credit_send = true;
            } else {
               reset_stream_locked(state);
               over_credit = true;
            }
         } else {
            if (!payload.empty()) {
               state->receive_window -= static_cast<std::uint32_t>(payload.size());
               if (exceeds_limit(state->buffered, payload.size(), options_.max_stream_buffer) ||
                   exceeds_limit(session_buffer_, payload.size(), options_.max_session_buffer)) {
                  reset_stream_locked(state);
                  needs_reset = true;
               } else {
                  state->inbound.push_back(payload);
                  state->buffered += payload.size();
                  session_buffer_ += payload.size();
               }
            }
            if ((header.flags & detail::fin) != 0U) {
               state->remote_fin = true;
            }
            state->read_notification.notify();
         }
      }
      if (over_credit) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux peer exceeded receive window");
      }
      if (!wait_for_credit_send) {
         break;
      }
      (void)co_await state->receive_credit_notification.async_wait(observed);
      {
         auto lock = std::scoped_lock{mutex_};
         rethrow_terminal_locked();
      }
   }

   if (needs_reset) {
      co_await write_frame(detail::frame_type::data, detail::rst, header.stream_id, 0);
   }
}

boost::asio::awaitable<void> session::impl::handle_window_update(const detail::frame_header& header) {
   if (header.stream_id == 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "invalid yamux window update frame");
   }

   auto opened = std::shared_ptr<stream_state>{};
   if ((header.flags & detail::syn) != 0U) {
      const auto send_window = checked_peer_window(detail::initial_stream_window, header.length);
      opened = co_await handle_stream_open(header, send_window);
      if (!opened) {
         co_return;
      }
   }

   if ((header.flags & detail::rst) != 0U) {
      auto lock = std::scoped_lock{mutex_};
      if (const auto found = streams_.find(header.stream_id);
          found != streams_.end() && !found->second->reset &&
          !(found->second->local_fin && found->second->remote_fin)) {
         reset_stream_locked(found->second);
      }
      co_return;
   }

   {
      auto lock = std::scoped_lock{mutex_};
      auto state = opened;
      if (!state) {
         if (const auto found = streams_.find(header.stream_id); found != streams_.end()) {
            state = found->second;
         } else {
            co_return;
         }
      }
      if (state->reset || (state->local_fin && state->remote_fin)) {
         co_return;
      }
      if (!opened && header.length > 0) {
         const auto updated = checked_peer_window(state->send_window, header.length);
         state->send_window = updated;
      }
      if ((header.flags & detail::fin) != 0U) {
         state->remote_fin = true;
      }
      state->read_notification.notify();
      state->window_notification.notify();
   }
}

boost::asio::awaitable<std::shared_ptr<session::impl::stream_state>>
session::impl::handle_stream_open(const detail::frame_header& header, std::uint32_t send_window) {
   if (!remote_opens_stream(side_, header.stream_id)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux stream id has invalid parity");
   }

   auto reject = false;
   auto state = std::shared_ptr<stream_state>{};
   {
      auto lock = std::scoped_lock{mutex_};
      reclaim_closed_streams_locked();
      if (streams_.contains(header.stream_id)) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux stream already exists");
      }
      if (streams_.size() >= options_.max_streams || pending_accepts_.size() >= options_.max_pending_accepts) {
         reject = true;
      } else {
         state = make_stream_locked(header.stream_id, send_window);
         streams_.emplace(header.stream_id, state);
         pending_accepts_.push_back(header.stream_id);
         accept_notification_.notify();
      }
   }

   if (reject) {
      co_await write_frame(detail::frame_type::data, detail::rst, header.stream_id, 0);
      co_return std::shared_ptr<stream_state>{};
   }
   co_await write_frame(detail::frame_type::window_update, detail::ack, header.stream_id, local_window_delta());
   co_return state;
}

boost::asio::awaitable<void> session::impl::handle_ping(const detail::frame_header& header) {
   if (header.stream_id != 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux ping must use stream zero");
   }
   if ((header.flags & detail::ack) == 0U) {
      co_await write_frame(detail::frame_type::ping, detail::ack, 0, header.length);
   }
}

[[noreturn]] void session::impl::handle_go_away(const detail::frame_header& header) {
   if (header.stream_id != 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "yamux goaway must use stream zero");
   }
   const auto code =
       header.length == detail::go_away_normal ? exceptions::code::closed : exceptions::code::protocol_error;
   fail_session(code, "yamux remote sent goaway");
   auto lock = std::scoped_lock{mutex_};
   rethrow_terminal_locked();
   std::terminate();
}

} // namespace forge::net::yamux
