module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
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

module fcl.yamux.session;

import fcl.transport.exceptions;

namespace fcl::yamux {

namespace {

using bytes = std::vector<std::uint8_t>;

enum class frame_type : std::uint8_t {
   data = 0,
   window_update = 1,
   ping = 2,
   go_away = 3,
};

inline constexpr std::uint8_t version = 0;
inline constexpr std::uint16_t syn = 0x01;
inline constexpr std::uint16_t ack = 0x02;
inline constexpr std::uint16_t fin = 0x04;
inline constexpr std::uint16_t rst = 0x08;
inline constexpr std::uint16_t known_flags = syn | ack | fin | rst;
inline constexpr std::size_t header_size = 12;
inline constexpr std::uint32_t go_away_normal = 0;
inline constexpr std::uint32_t go_away_protocol = 1;
inline constexpr std::uint32_t go_away_internal = 2;

[[nodiscard]] std::chrono::steady_clock::time_point far_future() noexcept {
   return (std::chrono::steady_clock::time_point::max)();
}

void arm_wait(const std::shared_ptr<boost::asio::steady_timer>& timer) {
   if (timer) {
      timer->expires_at(far_future());
   }
}

void notify_waiters(const std::shared_ptr<boost::asio::steady_timer>& timer) {
   if (timer) {
      timer->expires_at(std::chrono::steady_clock::now());
   }
}

struct frame_header {
   frame_type type = frame_type::data;
   std::uint16_t flags = 0;
   std::uint32_t stream_id = 0;
   std::uint32_t length = 0;
};

[[nodiscard]] std::uint32_t load_u32(std::span<const std::uint8_t> value, std::size_t offset) noexcept {
   return (static_cast<std::uint32_t>(value[offset]) << 24U) |
          (static_cast<std::uint32_t>(value[offset + 1]) << 16U) |
          (static_cast<std::uint32_t>(value[offset + 2]) << 8U) |
          static_cast<std::uint32_t>(value[offset + 3]);
}

void append_u32(bytes& out, std::uint32_t value) {
   out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

[[nodiscard]] bytes encode_frame(frame_type type, std::uint16_t flags, std::uint32_t stream_id,
                                 std::uint32_t length, std::span<const std::uint8_t> payload = {}) {
   auto out = bytes{};
   out.reserve(header_size + payload.size());
   out.push_back(version);
   out.push_back(static_cast<std::uint8_t>(type));
   out.push_back(static_cast<std::uint8_t>((flags >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(flags & 0xffU));
   append_u32(out, stream_id);
   append_u32(out, length);
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

[[nodiscard]] bool remote_opens_stream(side local_side, std::uint32_t stream_id) noexcept {
   const auto remote_is_initiator = local_side == side::responder;
   const auto id_is_odd = (stream_id % 2U) == 1U;
   return remote_is_initiator == id_is_odd;
}

[[nodiscard]] std::exception_ptr make_exception(exceptions::code value, std::string message) {
   try {
      switch (value) {
      case exceptions::code::invalid_options:
         FCL_THROW_EXCEPTION(exceptions::invalid_options, message);
      case exceptions::code::protocol_error:
         FCL_THROW_EXCEPTION(exceptions::protocol_error, message);
      case exceptions::code::resource_limit:
         FCL_THROW_EXCEPTION(exceptions::resource_limit, message);
      case exceptions::code::stream_reset:
         FCL_THROW_EXCEPTION(exceptions::stream_reset, message);
      case exceptions::code::closed:
         FCL_THROW_EXCEPTION(exceptions::closed, message);
      case exceptions::code::canceled:
         FCL_THROW_EXCEPTION(exceptions::canceled, message);
      }
   } catch (...) {
      return std::current_exception();
   }
   return {};
}

} // namespace

struct session::impl : std::enable_shared_from_this<impl> {
   struct stream_state {
      explicit stream_state(std::uint32_t stream_id) : id(stream_id) {}

      std::uint32_t id = 0;
      std::uint32_t send_window = 0;
      std::deque<bytes> inbound;
      std::size_t buffered = 0;
      bool local_fin = false;
      bool remote_fin = false;
      bool reset = false;
      bool accepted = false;
      std::shared_ptr<boost::asio::steady_timer> read_timer;
      std::shared_ptr<boost::asio::steady_timer> window_timer;
   };

   struct write_waiter {
      explicit write_waiter(boost::asio::any_io_executor executor)
          : timer(std::make_shared<boost::asio::steady_timer>(std::move(executor), far_future())) {}

      std::shared_ptr<boost::asio::steady_timer> timer;
      bool ready = false;
      bool canceled = false;
   };

   impl(transport::stream stream, side session_side, options session_options)
       : stream_(std::move(stream)), side_(session_side), options_(session_options) {
      validate_options();
      next_stream_id_ = side_ == side::initiator ? 1U : 2U;
   }

   [[nodiscard]] bool valid() const noexcept {
      auto lock = std::scoped_lock{mutex_};
      return stream_.valid() && !closed_ && !canceled_;
   }

   boost::asio::awaitable<void> ensure_started() {
      auto executor = co_await boost::asio::this_coro::executor;
      auto start = false;
      {
         auto lock = std::scoped_lock{mutex_};
         if (!executor_) {
            executor_ = executor;
            accept_timer_ = std::make_shared<boost::asio::steady_timer>(executor, far_future());
         }
         if (!started_) {
            started_ = true;
            start = true;
         }
      }
      if (start) {
         auto self = shared_from_this();
         boost::asio::co_spawn(
             executor,
             [self]() -> boost::asio::awaitable<void> {
                co_await self->read_loop();
             },
             boost::asio::detached);
      }
   }

   boost::asio::awaitable<transport::stream> async_open_stream() {
      co_await ensure_started();

      std::shared_ptr<stream_state> state;
      {
         auto lock = std::scoped_lock{mutex_};
         rethrow_terminal_locked();
         reclaim_closed_streams_locked();
         if (streams_.size() >= options_.max_streams) {
            FCL_THROW_EXCEPTION(exceptions::resource_limit, "yamux maximum stream count reached");
         }
         const auto id = next_stream_id_;
         next_stream_id_ += 2U;
         state = make_stream_locked(id, options_.initial_window);
         state->accepted = true;
         streams_.emplace(id, state);
      }

      co_await write_frame(frame_type::window_update, syn, state->id, options_.initial_window);
      co_return make_transport_stream(state->id);
   }

   boost::asio::awaitable<transport::stream> async_accept_stream() {
      co_await ensure_started();

      auto error = boost::system::error_code{};
      while (true) {
         {
            auto lock = std::scoped_lock{mutex_};
            if (!pending_accepts_.empty()) {
               const auto id = pending_accepts_.front();
               pending_accepts_.pop_front();
               if (const auto found = streams_.find(id); found != streams_.end()) {
                  found->second->accepted = true;
               }
               co_return make_transport_stream(id);
            }
            rethrow_terminal_locked();
            arm_wait(accept_timer_);
         }

         co_await accept_timer_->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (error && error != boost::asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   boost::asio::awaitable<void> async_close() {
      co_await ensure_started();
      const auto should_send = mark_closing();
      if (should_send) {
         try {
            co_await write_frame(frame_type::go_away, 0, 0, go_away_normal, {}, true);
         } catch (...) {
            // Closing is best-effort once the underlying byte stream has already failed.
         }
      }
      try {
         co_await stream_.async_close();
      } catch (...) {
      }
      fail_session(exceptions::code::closed, "yamux session closed");
      co_return;
   }

   void cancel() {
      fail_session(exceptions::code::canceled, "yamux session canceled");
      if (executor_) {
         auto self = shared_from_this();
         boost::asio::co_spawn(
             *executor_,
             [self]() -> boost::asio::awaitable<void> {
                try {
                   co_await self->stream_.async_close();
                } catch (...) {
                }
             },
             boost::asio::detached);
      }
   }

   boost::asio::awaitable<void> write_stream(std::uint32_t id, bytes payload) {
      co_await ensure_started();

      auto offset = std::size_t{0};
      while (offset < payload.size()) {
         auto chunk_size = std::size_t{0};
         {
            auto lock = std::scoped_lock{mutex_};
            auto state = get_stream_locked(id);
            if (state->local_fin) {
               FCL_THROW_EXCEPTION(exceptions::closed, "yamux stream is locally closed");
            }
            if (state->reset) {
               FCL_THROW_EXCEPTION(exceptions::stream_reset, "yamux stream reset");
            }
         }

         auto error = boost::system::error_code{};
         while (true) {
            {
               auto lock = std::scoped_lock{mutex_};
               auto state = get_stream_locked(id);
               rethrow_terminal_locked();
               if (state->reset) {
                  FCL_THROW_EXCEPTION(exceptions::stream_reset, "yamux stream reset");
               }
               if (state->send_window > 0) {
                  chunk_size = std::min<std::size_t>(
                      {payload.size() - offset, options_.max_frame_size, state->send_window});
                  state->send_window -= static_cast<std::uint32_t>(chunk_size);
                  break;
               }
               arm_wait(state->window_timer);
            }

            const auto timer = window_timer_for(id);
            co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
            if (error && error != boost::asio::error::operation_aborted) {
               throw boost::system::system_error{error};
            }
         }

         auto chunk = std::span<const std::uint8_t>{payload.data() + offset, chunk_size};
         co_await write_frame(frame_type::data, 0, id, static_cast<std::uint32_t>(chunk_size), chunk);
         offset += chunk_size;
      }
   }

   boost::asio::awaitable<bytes> read_stream(std::uint32_t id) {
      co_await ensure_started();

      auto error = boost::system::error_code{};
      while (true) {
         auto consumed = std::uint32_t{0};
         auto out = bytes{};
         {
            auto lock = std::scoped_lock{mutex_};
            auto state = get_stream_locked(id);
            if (!state->inbound.empty()) {
               out = std::move(state->inbound.front());
               state->inbound.pop_front();
               state->buffered -= out.size();
               session_buffer_ -= out.size();
               consumed = static_cast<std::uint32_t>(out.size());
            } else if (state->reset) {
               FCL_THROW_EXCEPTION(exceptions::stream_reset, "yamux stream reset");
            } else if (state->remote_fin) {
               FCL_THROW_EXCEPTION(exceptions::closed, "yamux stream closed by remote");
            } else {
               rethrow_terminal_locked();
               arm_wait(state->read_timer);
            }
         }

         if (!out.empty()) {
            co_await write_frame(frame_type::window_update, 0, id, consumed);
            co_return out;
         }

         const auto timer = read_timer_for(id);
         co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (error && error != boost::asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   boost::asio::awaitable<void> close_stream(std::uint32_t id) {
      co_await ensure_started();
      {
         auto lock = std::scoped_lock{mutex_};
         auto state = get_stream_locked(id);
         if (state->local_fin || state->reset) {
            co_return;
         }
         state->local_fin = true;
      }
      co_await write_frame(frame_type::data, fin, id, 0);
   }

 private:
   class stream_model final : public transport::detail::stream_concept {
    public:
      stream_model(std::shared_ptr<impl> owner, std::uint32_t stream_id)
          : owner_(std::move(owner)), stream_id_(stream_id) {}

      [[nodiscard]] bool valid() const noexcept override {
         auto owner = owner_.lock();
         return owner && owner->stream_valid(stream_id_);
      }

      [[nodiscard]] std::int64_t id() const noexcept override {
         return static_cast<std::int64_t>(stream_id_);
      }

      boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> value) override {
         auto owner = owner_.lock();
         if (!owner) {
            FCL_THROW_EXCEPTION(exceptions::closed, "yamux session expired");
         }
         auto owned = bytes{value.begin(), value.end()};
         co_await owner->write_stream(stream_id_, std::move(owned));
      }

      boost::asio::awaitable<bytes> async_read() override {
         auto owner = owner_.lock();
         if (!owner) {
            FCL_THROW_EXCEPTION(exceptions::closed, "yamux session expired");
         }
         co_return co_await owner->read_stream(stream_id_);
      }

      boost::asio::awaitable<void> async_close() override {
         auto owner = owner_.lock();
         if (owner) {
            co_await owner->close_stream(stream_id_);
         }
      }

      void cancel() override {
         auto owner = owner_.lock();
         if (owner) {
            owner->reset_stream(stream_id_);
         }
      }

    private:
      std::weak_ptr<impl> owner_;
      std::uint32_t stream_id_ = 0;
   };

   void validate_options() const {
      if (!stream_.valid()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "yamux requires a valid transport stream");
      }
      if (options_.initial_window == 0 || options_.max_stream_window < options_.initial_window ||
          options_.max_frame_size == 0 || options_.max_streams == 0 || options_.max_pending_accepts == 0 ||
          options_.max_stream_buffer == 0 || options_.max_session_buffer == 0) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "invalid yamux options");
      }
      if (options_.max_frame_size > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "yamux frame size exceeds wire limit");
      }
   }

   [[nodiscard]] std::shared_ptr<stream_state> make_stream_locked(std::uint32_t id, std::uint32_t send_window) {
      auto state = std::make_shared<stream_state>(id);
      state->send_window = std::min(send_window, options_.max_stream_window);
      if (executor_) {
         state->read_timer = std::make_shared<boost::asio::steady_timer>(*executor_, far_future());
         state->window_timer = std::make_shared<boost::asio::steady_timer>(*executor_, far_future());
      }
      return state;
   }

   [[nodiscard]] std::shared_ptr<stream_state> get_stream_locked(std::uint32_t id) const {
      const auto found = streams_.find(id);
      if (found == streams_.end()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "yamux stream does not exist");
      }
      return found->second;
   }

   [[nodiscard]] std::shared_ptr<boost::asio::steady_timer> read_timer_for(std::uint32_t id) {
      auto lock = std::scoped_lock{mutex_};
      auto state = get_stream_locked(id);
      return state->read_timer;
   }

   [[nodiscard]] std::shared_ptr<boost::asio::steady_timer> window_timer_for(std::uint32_t id) {
      auto lock = std::scoped_lock{mutex_};
      auto state = get_stream_locked(id);
      return state->window_timer;
   }

   [[nodiscard]] bool stream_valid(std::uint32_t id) const noexcept {
      auto lock = std::scoped_lock{mutex_};
      const auto found = streams_.find(id);
      return found != streams_.end() && !closed_ && !canceled_ && !found->second->reset;
   }

   [[nodiscard]] transport::stream make_transport_stream(std::uint32_t id) {
      return transport::detail::stream_access::make(std::make_shared<stream_model>(shared_from_this(), id));
   }

   void rethrow_terminal_locked() const {
      if (terminal_error_) {
         std::rethrow_exception(terminal_error_);
      }
      if (canceled_) {
         FCL_THROW_EXCEPTION(exceptions::canceled, "yamux session canceled");
      }
      if (closed_) {
         FCL_THROW_EXCEPTION(exceptions::closed, "yamux session closed");
      }
   }

   [[nodiscard]] bool mark_closing() {
      auto lock = std::scoped_lock{mutex_};
      if (closed_ || canceled_) {
         return false;
      }
      closed_ = true;
      wake_all_locked();
      return true;
   }

   void fail_session(exceptions::code value, std::string message) {
      {
         auto lock = std::scoped_lock{mutex_};
         if (!terminal_error_) {
            terminal_error_ = make_exception(value, std::move(message));
         }
         if (value == exceptions::code::canceled) {
            canceled_ = true;
         } else {
            closed_ = true;
         }
         for (const auto& [_, state] : streams_) {
            state->reset = value == exceptions::code::protocol_error || value == exceptions::code::resource_limit;
         }
         wake_all_locked();
      }
   }

   void wake_all_locked() {
      if (accept_timer_) {
         notify_waiters(accept_timer_);
      }
      for (const auto& waiter : write_waiters_) {
         waiter->ready = true;
         waiter->canceled = true;
         if (waiter->timer) {
            waiter->timer->cancel();
         }
      }
      write_waiters_.clear();
      for (const auto& [_, state] : streams_) {
         if (state->read_timer) {
            notify_waiters(state->read_timer);
         }
         if (state->window_timer) {
            notify_waiters(state->window_timer);
         }
      }
   }

   boost::asio::awaitable<void> acquire_write_slot(bool allow_after_close) {
      auto executor = co_await boost::asio::this_coro::executor;
      auto waiter = std::shared_ptr<write_waiter>{};
      auto error = boost::system::error_code{};
      while (true) {
         {
            auto lock = std::scoped_lock{mutex_};
            if (!allow_after_close) {
               rethrow_terminal_locked();
            }
            if (!waiter) {
               if (!write_busy_) {
                  write_busy_ = true;
                  co_return;
               }
               waiter = std::make_shared<write_waiter>(executor);
               write_waiters_.push_back(waiter);
            }
            if (waiter->ready) {
               if (waiter->canceled) {
                  if (!allow_after_close) {
                     rethrow_terminal_locked();
                  }
                  FCL_THROW_EXCEPTION(exceptions::closed, "yamux write waiter canceled");
               }
               co_return;
            }
         }

         co_await waiter->timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (error && error != boost::asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   boost::asio::awaitable<void> write_frame(frame_type type, std::uint16_t flags, std::uint32_t stream_id,
                                           std::uint32_t length, std::span<const std::uint8_t> payload = {},
                                           bool allow_after_close = false) {
      auto encoded = encode_frame(type, flags, stream_id, length, payload);

      co_await acquire_write_slot(allow_after_close);

      try {
         co_await stream_.async_write(encoded);
      } catch (...) {
         finish_write();
         fail_session(exceptions::code::closed, "yamux underlying stream write failed");
         throw;
      }
      finish_write();
   }

   void finish_write() {
      auto next = std::shared_ptr<write_waiter>{};
      {
         auto lock = std::scoped_lock{mutex_};
         if (!write_waiters_.empty()) {
            next = write_waiters_.front();
            write_waiters_.pop_front();
            next->ready = true;
         } else {
            write_busy_ = false;
         }
      }
      if (next && next->timer) {
         next->timer->cancel();
      }
   }

   boost::asio::awaitable<void> read_loop() {
      auto terminal = exceptions::code::closed;
      auto message = std::string{"yamux read loop stopped"};
      auto go_away = std::optional<std::uint32_t>{};
      try {
         auto buffer = bytes{};
         while (true) {
            const auto next = co_await read_frame(buffer);
            co_await handle_frame(next.first, next.second);
         }
      } catch (const exceptions::resource_limit&) {
         terminal = exceptions::code::resource_limit;
         message = "yamux resource limit exceeded";
         go_away = go_away_internal;
      } catch (const exceptions::protocol_error&) {
         terminal = exceptions::code::protocol_error;
         message = "yamux protocol error";
         go_away = go_away_protocol;
      } catch (const transport::exceptions::closed&) {
         terminal = exceptions::code::closed;
         message = "yamux underlying stream closed";
      } catch (...) {
         terminal = exceptions::code::closed;
         message = "yamux read loop stopped";
      }
      if (go_away) {
         try {
            co_await write_frame(frame_type::go_away, 0, 0, *go_away, {}, true);
         } catch (...) {
         }
      }
      fail_session(terminal, std::move(message));
   }

   boost::asio::awaitable<std::pair<frame_header, bytes>> read_frame(bytes& buffer) {
      while (buffer.size() < header_size) {
         auto chunk = co_await stream_.async_read();
         if (chunk.empty()) {
            continue;
         }
         buffer.insert(buffer.end(), chunk.begin(), chunk.end());
      }

      auto view = std::span<const std::uint8_t>{buffer.data(), buffer.size()};
      if (view[0] != version) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux frame version mismatch");
      }
      if (view[1] > static_cast<std::uint8_t>(frame_type::go_away)) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux frame type is invalid");
      }
      const auto flags = static_cast<std::uint16_t>((static_cast<std::uint16_t>(view[2]) << 8U) | view[3]);
      if ((flags & ~known_flags) != 0U) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux frame has unknown flags");
      }

      auto header = frame_header{
          .type = static_cast<frame_type>(view[1]),
          .flags = flags,
          .stream_id = load_u32(view, 4),
          .length = load_u32(view, 8),
      };

      if (header.type == frame_type::data && header.length > options_.max_frame_size) {
         FCL_THROW_EXCEPTION(exceptions::resource_limit, "yamux frame exceeds maximum size");
      }
      const auto payload_size = header.type == frame_type::data ? static_cast<std::size_t>(header.length) : 0U;
      while (buffer.size() < header_size + payload_size) {
         auto chunk = co_await stream_.async_read();
         if (chunk.empty()) {
            continue;
         }
         buffer.insert(buffer.end(), chunk.begin(), chunk.end());
      }

      auto payload = bytes{};
      if (payload_size > 0) {
         payload.insert(payload.end(), buffer.begin() + static_cast<std::ptrdiff_t>(header_size),
                        buffer.begin() + static_cast<std::ptrdiff_t>(header_size + payload_size));
      }
      buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(header_size + payload_size));
      co_return std::pair{header, std::move(payload)};
   }

   boost::asio::awaitable<void> handle_frame(const frame_header& header, const bytes& payload) {
      switch (header.type) {
      case frame_type::data:
         co_await handle_data(header, payload);
         co_return;
      case frame_type::window_update:
         co_await handle_window_update(header);
         co_return;
      case frame_type::ping:
         co_await handle_ping(header);
         co_return;
      case frame_type::go_away:
         handle_go_away(header);
         co_return;
      }
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "unknown yamux frame type");
   }

   boost::asio::awaitable<void> handle_data(const frame_header& header, const bytes& payload) {
      if (header.stream_id == 0 || header.length != payload.size()) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "invalid yamux data frame");
      }

      if ((header.flags & rst) != 0U) {
         reset_stream(header.stream_id);
         co_return;
      }

      auto opened = std::shared_ptr<stream_state>{};
      if ((header.flags & syn) != 0U) {
         opened = co_await handle_stream_open(header, options_.initial_window);
         if (!opened) {
            co_return;
         }
      }

      auto needs_reset = false;
      {
         auto lock = std::scoped_lock{mutex_};
         auto state = opened ? opened : get_stream_locked(header.stream_id);
         if (!payload.empty()) {
            if (state->buffered + payload.size() > options_.max_stream_buffer ||
                session_buffer_ + payload.size() > options_.max_session_buffer) {
               state->reset = true;
               needs_reset = true;
            } else {
               state->inbound.push_back(payload);
               state->buffered += payload.size();
               session_buffer_ += payload.size();
            }
         }
         if ((header.flags & fin) != 0U) {
            state->remote_fin = true;
         }
         if (state->read_timer) {
            notify_waiters(state->read_timer);
         }
      }

      if (needs_reset) {
         co_await write_frame(frame_type::data, rst, header.stream_id, 0);
         FCL_THROW_EXCEPTION(exceptions::resource_limit, "yamux stream buffer limit exceeded");
      }
   }

   boost::asio::awaitable<void> handle_window_update(const frame_header& header) {
      if (header.stream_id == 0) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "invalid yamux window update frame");
      }

      auto opened = std::shared_ptr<stream_state>{};
      if ((header.flags & syn) != 0U) {
         auto send_window = options_.initial_window;
         if (header.length > 0U) {
            send_window = std::min(header.length, options_.max_stream_window);
         }
         opened = co_await handle_stream_open(header, send_window);
         if (!opened) {
            co_return;
         }
      }

      if ((header.flags & rst) != 0U) {
         reset_stream(header.stream_id);
         co_return;
      }

      {
         auto lock = std::scoped_lock{mutex_};
         auto state = opened ? opened : get_stream_locked(header.stream_id);
         if ((header.flags & syn) == 0U && (header.flags & ack) == 0U && header.length > 0) {
            const auto available = options_.max_stream_window - state->send_window;
            state->send_window += std::min(header.length, available);
         }
         if ((header.flags & fin) != 0U) {
            state->remote_fin = true;
         }
         if (state->read_timer) {
            notify_waiters(state->read_timer);
         }
         if (state->window_timer) {
            notify_waiters(state->window_timer);
         }
      }
   }

   boost::asio::awaitable<std::shared_ptr<stream_state>>
   handle_stream_open(const frame_header& header, std::uint32_t send_window) {
      if (!remote_opens_stream(side_, header.stream_id)) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux stream id has invalid parity");
      }

      auto reject = false;
      auto state = std::shared_ptr<stream_state>{};
      {
         auto lock = std::scoped_lock{mutex_};
         reclaim_closed_streams_locked();
         if (streams_.contains(header.stream_id)) {
            FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux stream already exists");
         }
         if (streams_.size() >= options_.max_streams || pending_accepts_.size() >= options_.max_pending_accepts) {
            reject = true;
         } else {
            state = make_stream_locked(header.stream_id, send_window);
            streams_.emplace(header.stream_id, state);
            pending_accepts_.push_back(header.stream_id);
            if (accept_timer_) {
               notify_waiters(accept_timer_);
            }
         }
      }

      if (reject) {
         co_await write_frame(frame_type::data, rst, header.stream_id, 0);
         co_return std::shared_ptr<stream_state>{};
      }
      co_await write_frame(frame_type::window_update, ack, header.stream_id, options_.initial_window);
      co_return state;
   }

   [[nodiscard]] bool is_reclaimable_stream_locked(const stream_state& state) const noexcept {
      if (state.reset) {
         return true;
      }
      return state.local_fin && state.remote_fin && state.inbound.empty();
   }

   void reclaim_closed_streams_locked() {
      for (auto it = streams_.begin(); it != streams_.end();) {
         auto& state = *it->second;
         if (!is_reclaimable_stream_locked(state)) {
            ++it;
            continue;
         }
         std::erase(pending_accepts_, it->first);
         if (state.buffered > 0) {
            session_buffer_ = state.buffered > session_buffer_ ? 0 : session_buffer_ - state.buffered;
            state.buffered = 0;
            state.inbound.clear();
         }
         if (state.read_timer) {
            notify_waiters(state.read_timer);
         }
         if (state.window_timer) {
            notify_waiters(state.window_timer);
         }
         it = streams_.erase(it);
      }
   }

   boost::asio::awaitable<void> handle_ping(const frame_header& header) {
      if (header.stream_id != 0) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux ping must use stream zero");
      }
      if ((header.flags & ack) == 0U) {
         co_await write_frame(frame_type::ping, ack, 0, header.length);
      }
   }

   void handle_go_away(const frame_header& header) {
      if (header.stream_id != 0) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "yamux goaway must use stream zero");
      }
      const auto code = header.length == go_away_normal ? exceptions::code::closed : exceptions::code::protocol_error;
      fail_session(code, "yamux remote sent goaway");
   }

   void reset_stream(std::uint32_t id) {
      auto lock = std::scoped_lock{mutex_};
      const auto found = streams_.find(id);
      if (found == streams_.end()) {
         return;
      }
      found->second->reset = true;
      if (found->second->read_timer) {
         notify_waiters(found->second->read_timer);
      }
      if (found->second->window_timer) {
         notify_waiters(found->second->window_timer);
      }
   }

   transport::stream stream_;
   side side_ = side::initiator;
   options options_;

   mutable std::mutex mutex_;
   std::optional<boost::asio::any_io_executor> executor_;
   std::shared_ptr<boost::asio::steady_timer> accept_timer_;
   std::map<std::uint32_t, std::shared_ptr<stream_state>> streams_;
   std::deque<std::uint32_t> pending_accepts_;
   std::deque<std::shared_ptr<write_waiter>> write_waiters_;
   std::size_t session_buffer_ = 0;
   std::uint32_t next_stream_id_ = 1;
   bool started_ = false;
   bool closed_ = false;
   bool canceled_ = false;
   bool write_busy_ = false;
   std::exception_ptr terminal_error_;
};

class session_model final : public transport::detail::session_concept {
 public:
   explicit session_model(session value) : value_(std::move(value)) {}

   [[nodiscard]] bool valid() const noexcept override {
      return value_.valid();
   }

   boost::asio::awaitable<transport::stream> async_open_stream() override {
      co_return co_await value_.async_open_stream();
   }

   boost::asio::awaitable<transport::stream> async_accept_stream() override {
      co_return co_await value_.async_accept_stream();
   }

   boost::asio::awaitable<void> async_close() override {
      co_await value_.async_close();
   }

   void cancel() override {
      value_.cancel();
   }

 private:
   session value_;
};

session::session() = default;

session::session(transport::stream stream, side session_side, options session_options)
    : impl_(std::make_shared<impl>(std::move(stream), session_side, session_options)) {}

session::~session() = default;
session::session(session&&) noexcept = default;
session& session::operator=(session&&) noexcept = default;

bool session::valid() const noexcept {
   return impl_ && impl_->valid();
}

boost::asio::awaitable<transport::stream> session::async_open_stream() {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid yamux session");
   }
   co_return co_await impl_->async_open_stream();
}

boost::asio::awaitable<transport::stream> session::async_accept_stream() {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid yamux session");
   }
   co_return co_await impl_->async_accept_stream();
}

boost::asio::awaitable<void> session::async_close() {
   if (!impl_) {
      co_return;
   }
   co_await impl_->async_close();
}

void session::cancel() {
   if (impl_) {
      impl_->cancel();
   }
}

transport::session session::as_transport() && {
   return transport::detail::session_access::make(std::make_shared<session_model>(std::move(*this)));
}

transport::session make_session(transport::stream stream, side session_side, options session_options) {
   return session{std::move(stream), session_side, session_options}.as_transport();
}

} // namespace fcl::yamux
