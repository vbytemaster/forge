module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
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

module fcl.api.transport.client;

import fcl.raw.raw;
import fcl.transport.exceptions;
import fcl.transport.frame;

namespace fcl::api::transport {
namespace {

constexpr auto compact_threshold = std::size_t{65'536};

void compact_buffer(std::vector<std::uint8_t>& buffer, std::size_t& consumed) {
   if (consumed == 0) {
      return;
   }
   if (consumed >= buffer.size()) {
      buffer.clear();
      consumed = 0;
      return;
   }
   auto compacted = std::vector<std::uint8_t>{};
   compacted.reserve(buffer.size() - consumed);
   compacted.insert(compacted.end(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed), buffer.end());
   buffer = std::move(compacted);
   consumed = 0;
}

[[nodiscard]] std::span<const std::uint8_t> available_bytes(const std::vector<std::uint8_t>& buffer,
                                                            std::size_t consumed) noexcept {
   if (consumed >= buffer.size()) {
      return {};
   }
   return {buffer.data() + consumed, buffer.size() - consumed};
}

boost::asio::awaitable<fcl::transport::chunk> read_transport_frame(fcl::transport::stream& stream,
                                                                   std::vector<std::uint8_t>& buffer,
                                                                   std::size_t& consumed,
                                                                   std::uint32_t max_frame_size) {
   while (true) {
      const auto decoded = fcl::transport::decode_frame_view(available_bytes(buffer, consumed),
                                                             fcl::transport::frame_options{.max_size = max_frame_size});
      if (decoded.status == fcl::transport::frame_decode_status::complete) {
         const auto payload = fcl::transport::chunk{decoded.payload};
         consumed += decoded.consumed;
         if (consumed >= buffer.size() || consumed > compact_threshold) {
            compact_buffer(buffer, consumed);
         }
         co_return payload;
      }

      compact_buffer(buffer, consumed);
      auto next = co_await stream.async_read_chunk();
      auto view = next.bytes();
      buffer.insert(buffer.end(), view.begin(), view.end());
   }
}

boost::asio::awaitable<void> write_transport_frame(fcl::transport::stream& stream, std::span<const std::uint8_t> payload,
                                                  std::uint32_t max_frame_size) {
   auto encoded = std::vector<std::uint8_t>{};
   fcl::transport::encode_frame_to(encoded, payload, fcl::transport::frame_options{.max_size = max_frame_size});
   co_await stream.async_write(fcl::transport::chunk{std::move(encoded)});
}

[[nodiscard]] std::exception_ptr make_cancelled_error(const char* message) {
   try {
      FCL_THROW_EXCEPTION(exceptions::cancelled, message);
   } catch (...) {
      return std::current_exception();
   }
}

[[nodiscard]] std::chrono::milliseconds effective_deadline(call_options request_options, options client_options) {
   if (request_options.deadline.count() > 0) {
      return request_options.deadline;
   }
   return client_options.deadline;
}

[[nodiscard]] std::exception_ptr make_deadline_error(call_id id) {
   try {
      FCL_THROW_EXCEPTION(exceptions::deadline_exceeded, "API transport call deadline exceeded",
                          fcl::exceptions::ctx("call_id", id.value));
   } catch (...) {
      return std::current_exception();
   }
}

} // namespace

struct client::impl : std::enable_shared_from_this<client::impl> {
   struct pending_call {
      explicit pending_call(boost::asio::any_io_executor executor) : timer(std::move(executor)) {
         timer.expires_at(boost::asio::steady_timer::time_point::max());
      }

      boost::asio::steady_timer timer;
      std::optional<boost::asio::steady_timer::time_point> deadline_at;
      std::vector<frame> responses;
      std::exception_ptr error;
      bool done = false;
   };

   struct write_waiter {
      explicit write_waiter(boost::asio::any_io_executor executor) : timer(std::move(executor)) {
         timer.expires_at(boost::asio::steady_timer::time_point::max());
      }

      boost::asio::steady_timer timer;
      std::optional<boost::asio::steady_timer::time_point> deadline_at;
      call_id id;
      std::exception_ptr error;
      bool ready = false;
   };

   fcl::transport::stream stream;
   options settings;
   std::vector<std::uint8_t> read_buffer;
   std::size_t consumed = 0;
   std::uint64_t next_id = 1;
   std::unordered_map<std::uint64_t, std::shared_ptr<pending_call>> pending;
   std::deque<std::shared_ptr<write_waiter>> write_waiters;
   std::exception_ptr failure;
   mutable std::mutex mutex;
   bool reader_started = false;
   bool write_busy = false;
   bool canceled = false;

   [[nodiscard]] bool valid_locked() const noexcept {
      return !canceled && !failure && stream.valid();
   }

   [[nodiscard]] bool valid() const noexcept {
      auto lock = std::scoped_lock{mutex};
      return valid_locked();
   }

   [[nodiscard]] bool is_canceled() const noexcept {
      auto lock = std::scoped_lock{mutex};
      return canceled;
   }

   void fail_all(std::exception_ptr error) {
      if (!error) {
         return;
      }
      auto lock = std::scoped_lock{mutex};
      if (!failure) {
         failure = error;
      }
      for (auto& [_, pending_value] : pending) {
         pending_value->error = error;
         pending_value->done = true;
         pending_value->timer.cancel();
      }
      pending.clear();
      while (!write_waiters.empty()) {
         auto waiter = std::move(write_waiters.front());
         write_waiters.pop_front();
         waiter->error = error;
         waiter->ready = true;
         waiter->timer.cancel();
      }
      write_busy = false;
   }

   void fail_closed(std::exception_ptr error) {
      {
         auto lock = std::scoped_lock{mutex};
         canceled = true;
      }
      fail_all(error);
   }

   struct reservation {
      call_id id;
      std::shared_ptr<pending_call> pending;
      bool start_reader = false;
   };

   reservation reserve_call(frame& request, call_options& value, boost::asio::any_io_executor executor) {
      auto pending_value = std::make_shared<pending_call>(std::move(executor));
      auto lock = std::scoped_lock{mutex};
      if (!valid_locked()) {
         FCL_THROW_EXCEPTION(exceptions::cancelled, "API transport client is closed");
      }
      if (pending.size() >= settings.max_inflight) {
         FCL_THROW_EXCEPTION(exceptions::resource_exhausted, "API transport max inflight calls exceeded",
                             fcl::exceptions::ctx("max_inflight", settings.max_inflight));
      }
      if (value.id.value != 0) {
         request.id = value.id;
      } else if (request.id.value == 0) {
         request.id.value = next_id++;
      }
      if (!value.meta.empty()) {
         request.meta = std::move(value.meta);
      }
      request.codec = settings.codec;

      if (pending.contains(request.id.value)) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "duplicate active API transport call",
                             fcl::exceptions::ctx("call_id", request.id.value));
      }
      const auto deadline = effective_deadline(value, settings);
      if (deadline.count() > 0) {
         pending_value->timer.expires_after(deadline);
         pending_value->deadline_at = pending_value->timer.expiry();
      }
      pending.emplace(request.id.value, pending_value);

      auto start = false;
      if (!reader_started) {
         reader_started = true;
         start = true;
      }
      return reservation{.id = request.id, .pending = std::move(pending_value), .start_reader = start};
   }

   void remove_pending(call_id id, const std::shared_ptr<pending_call>& pending_value) {
      auto lock = std::scoped_lock{mutex};
      if (auto found = pending.find(id.value); found != pending.end() && found->second == pending_value) {
         pending.erase(found);
      }
   }

   void remove_write_waiter(const std::shared_ptr<write_waiter>& waiter) {
      auto lock = std::scoped_lock{mutex};
      auto found = std::find(write_waiters.begin(), write_waiters.end(), waiter);
      if (found != write_waiters.end()) {
         write_waiters.erase(found);
      }
   }

   [[nodiscard]] bool pending_done(const std::shared_ptr<pending_call>& pending_value) const {
      auto lock = std::scoped_lock{mutex};
      return pending_value->done;
   }

   [[nodiscard]] std::exception_ptr pending_error(const std::shared_ptr<pending_call>& pending_value) const {
      auto lock = std::scoped_lock{mutex};
      return pending_value->error;
   }

   [[nodiscard]] std::vector<frame> take_responses(const std::shared_ptr<pending_call>& pending_value) {
      auto lock = std::scoped_lock{mutex};
      return std::move(pending_value->responses);
   }

   void complete_pending(frame response) {
      if (response.kind != frame_kind::response && response.kind != frame_kind::error &&
          response.kind != frame_kind::stream_item && response.kind != frame_kind::stream_end) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "API transport received non-response frame",
                             fcl::exceptions::ctx("call_id", response.id.value));
      }
      if (response.codec != settings.codec) {
         FCL_THROW_EXCEPTION(exceptions::codec_failed, "API transport response codec is not accepted",
                             fcl::exceptions::ctx("codec", response.codec.value));
      }
      auto lock = std::scoped_lock{mutex};
      auto found = pending.find(response.id.value);
      if (found == pending.end()) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "API transport received unknown call_id",
                             fcl::exceptions::ctx("call_id", response.id.value));
      }
      auto pending_value = found->second;
      const auto terminal = response.kind == frame_kind::response || response.kind == frame_kind::error ||
                            response.kind == frame_kind::stream_end;
      pending_value->responses.push_back(std::move(response));
      if (!terminal) {
         return;
      }
      pending.erase(found);
      pending_value->done = true;
      pending_value->timer.cancel();
   }

   [[nodiscard]] bool no_pending_and_stop_reader() {
      auto lock = std::scoped_lock{mutex};
      if (!pending.empty()) {
         return false;
      }
      reader_started = false;
      return true;
   }

   void stop_reader() {
      auto lock = std::scoped_lock{mutex};
      reader_started = false;
   }

   boost::asio::awaitable<void> run_reader() {
      try {
         while (!is_canceled()) {
            auto payload = co_await read_transport_frame(stream, read_buffer, consumed, settings.max_frame_size);
            complete_pending(fcl::raw::unpack<frame>(payload.to_vector()));
            if (no_pending_and_stop_reader()) {
               co_return;
            }
         }
      } catch (...) {
         stop_reader();
         fail_all(std::current_exception());
         if (!is_canceled()) {
            stream.cancel();
         }
      }
   }

   void start_reader(boost::asio::any_io_executor executor) {
      boost::asio::co_spawn(
          std::move(executor),
          [self = shared_from_this()]() mutable -> boost::asio::awaitable<void> {
             co_await self->run_reader();
          },
          boost::asio::detached);
   }

   boost::asio::awaitable<void> acquire_write(call_id id, const std::shared_ptr<pending_call>& pending_value) {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto waiter = std::shared_ptr<write_waiter>{};
      {
         auto lock = std::scoped_lock{mutex};
         if (failure) {
            std::rethrow_exception(failure);
         }
         if (canceled) {
            FCL_THROW_EXCEPTION(exceptions::cancelled, "API transport client is closed");
         }
         if (pending_value->deadline_at &&
             boost::asio::steady_timer::clock_type::now() >= *pending_value->deadline_at) {
            std::rethrow_exception(make_deadline_error(id));
         }
         if (!write_busy) {
            write_busy = true;
            co_return;
         }
         waiter = std::make_shared<write_waiter>(executor);
         waiter->deadline_at = pending_value->deadline_at;
         waiter->id = id;
         if (waiter->deadline_at) {
            waiter->timer.expires_at(*waiter->deadline_at);
         }
         write_waiters.push_back(waiter);
      }

      while (true) {
         {
            auto lock = std::scoped_lock{mutex};
            if (waiter->ready) {
               if (waiter->error) {
                  std::rethrow_exception(waiter->error);
               }
               co_return;
            }
         }
         auto error = boost::system::error_code{};
         co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (!error) {
            remove_write_waiter(waiter);
            std::rethrow_exception(make_deadline_error(id));
         }
      }
   }

   void release_write() {
      auto lock = std::scoped_lock{mutex};
      const auto now = boost::asio::steady_timer::clock_type::now();
      while (!write_waiters.empty()) {
         auto waiter = std::move(write_waiters.front());
         write_waiters.pop_front();
         if (waiter->deadline_at && now >= *waiter->deadline_at) {
            waiter->error = make_deadline_error(waiter->id);
            waiter->ready = true;
            waiter->timer.cancel();
            continue;
         }
         waiter->ready = true;
         waiter->timer.cancel();
         return;
      }
      write_busy = false;
   }
};

client::client() = default;

client::client(fcl::transport::stream stream, options value) : impl_(std::make_shared<impl>()) {
   impl_->stream = std::move(stream);
   impl_->settings = std::move(value);
}

client::~client() = default;
client::client(client&&) noexcept = default;
client& client::operator=(client&&) noexcept = default;

bool client::valid() const noexcept {
   return impl_ && impl_->valid();
}

const options& client::settings() const noexcept {
   static const auto defaults = options{};
   return impl_ ? impl_->settings : defaults;
}

boost::asio::awaitable<std::vector<frame>> client::async_call_stream(frame request, call_options value) {
   auto self = impl_;
   if (!self) {
      FCL_THROW_EXCEPTION(exceptions::cancelled, "API transport client is closed");
   }
   const auto executor = co_await boost::asio::this_coro::executor;
   auto reservation = self->reserve_call(request, value, executor);
   if (reservation.start_reader) {
      self->start_reader(executor);
   }

   auto remove_pending = [&] {
      self->remove_pending(reservation.id, reservation.pending);
   };
   auto encoded = fcl::api::bytes{};
   fcl::raw::pack(encoded, request);

   try {
      co_await self->acquire_write(reservation.id, reservation.pending);
      try {
         co_await write_transport_frame(self->stream, encoded, self->settings.max_frame_size);
      } catch (...) {
         self->release_write();
         throw;
      }
      self->release_write();
   } catch (const exceptions::deadline_exceeded&) {
      remove_pending();
      auto timeout = std::current_exception();
      self->fail_all(timeout);
      self->stream.cancel();
      throw;
   } catch (...) {
      remove_pending();
      throw;
   }

   while (!self->pending_done(reservation.pending)) {
      auto error = boost::system::error_code{};
      co_await reservation.pending->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (!self->pending_done(reservation.pending) && !error) {
         remove_pending();
         auto timeout = std::exception_ptr{};
         try {
            FCL_THROW_EXCEPTION(exceptions::deadline_exceeded, "API transport call deadline exceeded",
                                fcl::exceptions::ctx("call_id", reservation.id.value));
         } catch (...) {
            timeout = std::current_exception();
         }
         self->fail_all(timeout);
         self->stream.cancel();
         std::rethrow_exception(timeout);
      }
   }
   if (auto error = self->pending_error(reservation.pending)) {
      std::rethrow_exception(error);
   }
   co_return self->take_responses(reservation.pending);
}

boost::asio::awaitable<frame> client::async_call(frame request, call_options value) {
   auto responses = co_await async_call_stream(std::move(request), std::move(value));
   for (auto& response : responses) {
      if (response.kind != frame_kind::stream_end) {
         co_return std::move(response);
      }
   }
   FCL_THROW_EXCEPTION(exceptions::protocol_error, "API transport streaming response has no item");
}

boost::asio::awaitable<void> client::async_close() {
   auto self = impl_;
   if (!self) {
      co_return;
   }
   self->fail_closed(make_cancelled_error("API transport client is closed"));
   co_await self->stream.async_close();
}

void client::cancel() {
   auto self = impl_;
   if (!self) {
      return;
   }
   self->fail_closed(make_cancelled_error("API transport client is cancelled"));
   self->stream.cancel();
}

} // namespace fcl::api::transport
