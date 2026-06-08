module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
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
#include <boost/asio/dispatch.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
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

void cancel_timer_on_strand(boost::asio::steady_timer& timer) {
   timer.cancel(); // on_strand
}

} // namespace

struct client::impl : std::enable_shared_from_this<client::impl> {
   using strand_type = boost::asio::strand<boost::asio::any_io_executor>;

   struct pending_call {
      explicit pending_call(const strand_type& executor) : timer(executor) {
         timer.expires_at(boost::asio::steady_timer::time_point::max());
      }

      boost::asio::steady_timer timer;
      std::optional<boost::asio::steady_timer::time_point> deadline_at;
      std::vector<frame> responses;
      std::exception_ptr error;
      bool done = false;
   };

   struct write_waiter {
      explicit write_waiter(const strand_type& executor) : timer(executor) {
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
   mutable std::mutex strand_mutex;
   std::optional<strand_type> strand;
   std::atomic_bool closed{false};
   bool reader_started = false;
   bool write_busy = false;
   bool canceled = false;

   [[nodiscard]] strand_type ensure_strand(boost::asio::any_io_executor executor) {
      auto lock = std::scoped_lock{strand_mutex};
      if (!strand) {
         strand.emplace(boost::asio::make_strand(std::move(executor)));
      }
      return *strand;
   }

   [[nodiscard]] std::optional<strand_type> current_strand() const {
      auto lock = std::scoped_lock{strand_mutex};
      return strand;
   }

   [[nodiscard]] bool valid() const noexcept {
      return !closed.load(std::memory_order_acquire) && stream.valid();
   }

   [[nodiscard]] bool valid_on_strand() const noexcept {
      return !closed.load(std::memory_order_acquire) && !canceled && !failure && stream.valid();
   }

   void fail_all_on_strand(std::exception_ptr error) {
      if (!error) {
         return;
      }
      closed.store(true, std::memory_order_release);
      if (!failure) {
         failure = error;
      }
      for (auto& [_, pending_value] : pending) {
         pending_value->error = error;
         pending_value->done = true;
         cancel_timer_on_strand(pending_value->timer);
      }
      pending.clear();
      while (!write_waiters.empty()) {
         auto waiter = std::move(write_waiters.front());
         write_waiters.pop_front();
         waiter->error = error;
         waiter->ready = true;
         cancel_timer_on_strand(waiter->timer);
      }
      write_busy = false;
   }

   void fail_closed_on_strand(std::exception_ptr error) {
      canceled = true;
      closed.store(true, std::memory_order_release);
      fail_all_on_strand(error);
   }

   struct reservation {
      call_id id;
      std::shared_ptr<pending_call> pending;
      bool start_reader = false;
   };

   reservation reserve_call_on_strand(frame& request, call_options& value, const strand_type& executor) {
      auto pending_value = std::make_shared<pending_call>(executor);
      if (!valid_on_strand()) {
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

   void remove_pending_on_strand(call_id id, const std::shared_ptr<pending_call>& pending_value) {
      if (auto found = pending.find(id.value); found != pending.end() && found->second == pending_value) {
         pending.erase(found);
      }
   }

   void remove_write_waiter_on_strand(const std::shared_ptr<write_waiter>& waiter) {
      auto found = std::find(write_waiters.begin(), write_waiters.end(), waiter);
      if (found != write_waiters.end()) {
         write_waiters.erase(found);
      }
   }

   void complete_pending_on_strand(frame response) {
      if (response.kind != frame_kind::response && response.kind != frame_kind::error &&
          response.kind != frame_kind::stream_item && response.kind != frame_kind::stream_end) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "API transport received non-response frame",
                             fcl::exceptions::ctx("call_id", response.id.value));
      }
      if (response.codec != settings.codec) {
         FCL_THROW_EXCEPTION(exceptions::codec_failed, "API transport response codec is not accepted",
                             fcl::exceptions::ctx("codec", response.codec.value));
      }
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
      cancel_timer_on_strand(pending_value->timer);
   }

   [[nodiscard]] bool no_pending_and_stop_reader_on_strand() {
      if (!pending.empty()) {
         return false;
      }
      reader_started = false;
      return true;
   }

   void stop_reader_on_strand() {
      reader_started = false;
   }

   boost::asio::awaitable<void> run_reader_on_strand() {
      try {
         while (!canceled) {
            auto payload = co_await read_transport_frame(stream, read_buffer, consumed, settings.max_frame_size);
            complete_pending_on_strand(fcl::raw::unpack<frame>(payload.to_vector()));
            if (no_pending_and_stop_reader_on_strand()) {
               co_return;
            }
         }
      } catch (...) {
         stop_reader_on_strand();
         fail_all_on_strand(std::current_exception());
         if (!canceled) {
            stream.cancel();
         }
      }
   }

   void start_reader_on_strand(const strand_type& executor) {
      boost::asio::co_spawn(
          std::move(executor),
          [self = shared_from_this()]() mutable -> boost::asio::awaitable<void> {
             co_await self->run_reader_on_strand();
          },
          boost::asio::detached);
   }

   boost::asio::awaitable<void> acquire_write_on_strand(call_id id,
                                                        const std::shared_ptr<pending_call>& pending_value,
                                                        const strand_type& executor) {
      auto waiter = std::shared_ptr<write_waiter>{};
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

      while (true) {
         if (waiter->ready) {
            if (waiter->error) {
               std::rethrow_exception(waiter->error);
            }
            co_return;
         }
         auto error = boost::system::error_code{};
         co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (!error) {
            remove_write_waiter_on_strand(waiter);
            std::rethrow_exception(make_deadline_error(id));
         }
      }
   }

   void release_write_on_strand() {
      const auto now = boost::asio::steady_timer::clock_type::now();
      while (!write_waiters.empty()) {
         auto waiter = std::move(write_waiters.front());
         write_waiters.pop_front();
         if (waiter->deadline_at && now >= *waiter->deadline_at) {
            waiter->error = make_deadline_error(waiter->id);
            waiter->ready = true;
            cancel_timer_on_strand(waiter->timer);
            continue;
         }
         waiter->ready = true;
         cancel_timer_on_strand(waiter->timer);
         return;
      }
      write_busy = false;
   }

   boost::asio::awaitable<std::vector<frame>> async_call_stream_on_strand(frame request, call_options value,
                                                                          const strand_type& executor) {
      auto reservation = reserve_call_on_strand(request, value, executor);
      if (reservation.start_reader) {
         start_reader_on_strand(executor);
      }

      auto encoded = fcl::api::bytes{};
      fcl::raw::pack(encoded, request);

      try {
         co_await acquire_write_on_strand(reservation.id, reservation.pending, executor);
         try {
            co_await write_transport_frame(stream, encoded, settings.max_frame_size);
         } catch (...) {
            release_write_on_strand();
            throw;
         }
         release_write_on_strand();
      } catch (const exceptions::deadline_exceeded&) {
         remove_pending_on_strand(reservation.id, reservation.pending);
         auto timeout = std::current_exception();
         fail_all_on_strand(timeout);
         stream.cancel();
         throw;
      } catch (...) {
         remove_pending_on_strand(reservation.id, reservation.pending);
         throw;
      }

      while (!reservation.pending->done) {
         auto error = boost::system::error_code{};
         co_await reservation.pending->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (!reservation.pending->done && !error) {
            remove_pending_on_strand(reservation.id, reservation.pending);
            auto timeout = make_deadline_error(reservation.id);
            fail_all_on_strand(timeout);
            stream.cancel();
            std::rethrow_exception(timeout);
         }
      }
      if (reservation.pending->error) {
         std::rethrow_exception(reservation.pending->error);
      }
      co_return std::move(reservation.pending->responses);
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
   auto strand = self->ensure_strand(executor);
   co_return co_await boost::asio::co_spawn(
       strand,
       [self, strand, request = std::move(request), value = std::move(value)]() mutable
           -> boost::asio::awaitable<std::vector<frame>> {
          co_return co_await self->async_call_stream_on_strand(std::move(request), std::move(value), strand);
       },
       boost::asio::use_awaitable);
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
   const auto executor = co_await boost::asio::this_coro::executor;
   auto strand = self->ensure_strand(executor);
   co_await boost::asio::co_spawn(
       strand,
       [self]() mutable -> boost::asio::awaitable<void> {
          self->fail_closed_on_strand(make_cancelled_error("API transport client is closed"));
          co_await self->stream.async_close();
       },
       boost::asio::use_awaitable);
}

void client::cancel() {
   auto self = impl_;
   if (!self) {
      return;
   }
   self->closed.store(true, std::memory_order_release);
   auto strand = self->current_strand();
   if (!strand) {
      self->stream.cancel();
      return;
   }
   boost::asio::dispatch(*strand, [self, error = make_cancelled_error("API transport client is cancelled")]() mutable {
      self->fail_closed_on_strand(error);
      self->stream.cancel();
   });
}

} // namespace fcl::api::transport
