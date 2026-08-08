#include <boost/test/unit_test.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.api.transport.exceptions;
import forge.api.transport.options;
import forge.api.transport.client;
import forge.api.transport.connection;
import forge.api.transport.server;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.raw.raw;
import forge.net.transport.exceptions;
import forge.net.transport.session;
import forge.net.transport.stream;

namespace transport_api_typed {

struct read_chunk {
   std::string ref;
};

struct chunk {
   std::string bytes;
};

template <typename Stream> Stream& operator<<(Stream& stream, const read_chunk& value) {
   forge::raw::pack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, read_chunk& value) {
   forge::raw::unpack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator<<(Stream& stream, const chunk& value) {
   forge::raw::pack(stream, value.bytes);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, chunk& value) {
   forge::raw::unpack(stream, value.bytes);
   return stream;
}

class cache_api : public forge::api::core::contract<cache_api, forge::api::core::surface::local |
                                                                   forge::api::core::surface::remote> {
 public:
   virtual ~cache_api() = default;
   virtual boost::asio::awaitable<chunk> read(read_chunk request) = 0;
};

class positional_api : public forge::api::core::contract<positional_api, forge::api::core::surface::local |
                                                                             forge::api::core::surface::remote> {
 public:
   virtual ~positional_api() = default;
   virtual boost::asio::awaitable<chunk> join(std::string left, std::string right) = 0;
};

} // namespace transport_api_typed

FORGE_API(::transport_api_typed::cache_api, FORGE_API_CONTRACT("cache", 1, 8), FORGE_API_METHOD(read))
FORGE_API(::transport_api_typed::positional_api, FORGE_API_CONTRACT("positional.transport", 1, 0),
          FORGE_API_METHOD(join, left, right))

namespace {

using bytes = std::vector<std::uint8_t>;
namespace protocol = transport_api_typed;
using cache_api = transport_api_typed::cache_api;
using positional_api = transport_api_typed::positional_api;

template <typename T> [[nodiscard]] forge::api::core::bytes pack_payload(const T& value) {
   return forge::api::core::pack_body(value);
}

class cache_impl final : public cache_api {
 public:
   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      co_return protocol::chunk{.bytes = request.ref + ":ok"};
   }
};

class positional_impl final : public positional_api {
 public:
   boost::asio::awaitable<protocol::chunk> join(std::string left, std::string right) override {
      co_return protocol::chunk{.bytes = std::move(left) + ":" + std::move(right) + ":ok"};
   }
};

struct cancellable_state {
   std::atomic_bool started{false};
   std::atomic_bool cancelled{false};
};

class cancellable_cache_impl final : public cache_api {
 public:
   explicit cancellable_cache_impl(std::shared_ptr<cancellable_state> value) : state_(std::move(value)) {}

   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      if (request.ref != "wait") {
         co_return protocol::chunk{.bytes = request.ref + ":ok"};
      }
      const auto executor = co_await boost::asio::this_coro::executor;
      auto timer = boost::asio::steady_timer{executor, boost::asio::steady_timer::time_point::max()};
      state_->started.store(true, std::memory_order_release);
      try {
         co_await timer.async_wait(boost::asio::use_awaitable);
      } catch (const boost::system::system_error& error) {
         if (error.code() == boost::asio::error::operation_aborted) {
            state_->cancelled.store(true, std::memory_order_release);
         }
         throw;
      }
      co_return protocol::chunk{.bytes = "unexpected"};
   }

 private:
   std::shared_ptr<cancellable_state> state_;
};

struct delayed_cancellation_state {
   std::atomic_bool started{false};
   std::atomic_bool cancelled{false};
   std::atomic_bool release{false};
   std::atomic_bool finished{false};
   std::weak_ptr<forge::api::core::registry> binding_owner;
   std::weak_ptr<cache_api> api_owner;
};

class delayed_cancellation_cache_impl final : public cache_api {
 public:
   explicit delayed_cancellation_cache_impl(std::shared_ptr<delayed_cancellation_state> value)
       : state_(std::move(value)) {}

   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      if (request.ref != "wait") {
         co_return protocol::chunk{.bytes = request.ref + ":ok"};
      }
      const auto executor = co_await boost::asio::this_coro::executor;
      auto timer = boost::asio::steady_timer{executor, boost::asio::steady_timer::time_point::max()};
      state_->started.store(true, std::memory_order_release);
      auto cancellation_observed = false;
      try {
         co_await timer.async_wait(boost::asio::use_awaitable);
      } catch (const boost::system::system_error& error) {
         if (error.code() != boost::asio::error::operation_aborted) {
            throw;
         }
         cancellation_observed = true;
         state_->cancelled.store(true, std::memory_order_release);
      }
      if (!cancellation_observed) {
         co_return protocol::chunk{.bytes = "unexpected"};
      }

      co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
      while (!state_->release.load(std::memory_order_acquire)) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }
      state_->finished.store(true, std::memory_order_release);
      co_return protocol::chunk{.bytes = "released"};
   }

 private:
   std::shared_ptr<delayed_cancellation_state> state_;
};

struct generation_state {
   std::atomic_bool old_started{false};
   std::atomic_bool old_cancelled{false};
   std::atomic_bool release_old{false};
   std::atomic_bool old_finished{false};
   std::atomic_bool new_started{false};
   std::atomic_bool release_new{false};
};

class generation_cache_impl final : public cache_api {
 public:
   explicit generation_cache_impl(std::shared_ptr<generation_state> value) : state_(std::move(value)) {}

   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto timer = boost::asio::steady_timer{executor};
      if (request.ref == "old") {
         state_->old_started.store(true, std::memory_order_release);
         timer.expires_at(boost::asio::steady_timer::time_point::max());
         try {
            co_await timer.async_wait(boost::asio::use_awaitable);
         } catch (const boost::system::system_error& error) {
            if (error.code() != boost::asio::error::operation_aborted) {
               throw;
            }
            state_->old_cancelled.store(true, std::memory_order_release);
         }
         co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
         while (!state_->release_old.load(std::memory_order_acquire)) {
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(boost::asio::use_awaitable);
         }
         state_->old_finished.store(true, std::memory_order_release);
         co_return protocol::chunk{.bytes = "old:released"};
      }

      state_->new_started.store(true, std::memory_order_release);
      while (!state_->release_new.load(std::memory_order_acquire)) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }
      co_return protocol::chunk{.bytes = request.ref + ":ok"};
   }

 private:
   std::shared_ptr<generation_state> state_;
};

struct gated_state {
   std::mutex mutex;
   std::size_t active = 0;
   std::size_t max_active = 0;
   std::size_t first_started = 0;
   std::size_t second_started = 0;
   bool release_first = false;
};

class gated_cache_impl final : public cache_api {
 public:
   explicit gated_cache_impl(std::shared_ptr<gated_state> value) : state_(std::move(value)) {}

   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto timer = boost::asio::steady_timer{executor};

      {
         auto lock = std::scoped_lock{state_->mutex};
         ++state_->active;
         state_->max_active = std::max(state_->max_active, state_->active);
         if (request.ref == "first") {
            ++state_->first_started;
         } else if (request.ref == "second") {
            ++state_->second_started;
         }
      }

      if (request.ref == "first") {
         while (true) {
            {
               auto lock = std::scoped_lock{state_->mutex};
               if (state_->release_first) {
                  break;
               }
            }
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(boost::asio::use_awaitable);
         }
      }

      {
         auto lock = std::scoped_lock{state_->mutex};
         --state_->active;
      }

      co_return protocol::chunk{.bytes = request.ref + ":ok"};
   }

 private:
   std::shared_ptr<gated_state> state_;
};

class fake_stream final : public forge::net::transport::detail::stream_concept {
 public:
   [[nodiscard]] bool valid() const noexcept override {
      auto lock = std::scoped_lock{mutex};
      return open;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return id_value;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> value) override {
      co_await wait_for_write_release();
      auto copy = bytes{value.begin(), value.end()};
      {
         auto lock = std::scoped_lock{mutex};
         writes.push_back(copy);
      }
      if (auto target = peer.lock()) {
         target->push_read(std::move(copy));
      }
      co_return;
   }

   boost::asio::awaitable<void> async_write_chunk(forge::net::transport::chunk value) override {
      co_await wait_for_write_release();
      auto copy = value.to_vector();
      {
         auto lock = std::scoped_lock{mutex};
         writes.push_back(copy);
      }
      if (auto target = peer.lock()) {
         target->push_read(std::move(copy));
      }
      co_return;
   }

   boost::asio::awaitable<bytes> async_read() override {
      co_return (co_await async_read_chunk()).into_vector();
   }

   boost::asio::awaitable<forge::net::transport::chunk> async_read_chunk() override {
      if (wait_for_reads) {
         {
            auto lock = std::scoped_lock{mutex};
            if (!reads.empty()) {
               auto out = std::move(reads.front());
               reads.pop_front();
               co_return forge::net::transport::chunk{std::move(out)};
            }
         }

         const auto executor = co_await boost::asio::this_coro::executor;
         while (true) {
            {
               auto lock = std::scoped_lock{mutex};
               if (!open || !reads.empty()) {
                  break;
               }
               if (!read_timer) {
                  read_timer = std::make_shared<boost::asio::steady_timer>(executor);
               }
            }
            read_timer->expires_at(boost::asio::steady_timer::time_point::max());
            auto error = boost::system::error_code{};
            co_await read_timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         }
      }
      auto lock = std::scoped_lock{mutex};
      if (reads.empty()) {
         open = false;
         FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, "fake stream closed");
      }
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return forge::net::transport::chunk{std::move(out)};
   }

   boost::asio::awaitable<void> async_close() override {
      auto lock = std::scoped_lock{mutex};
      open = false;
      co_return;
   }

   void cancel() override {
      std::shared_ptr<boost::asio::steady_timer> timer;
      {
         auto lock = std::scoped_lock{mutex};
         open = false;
         ++cancel_count;
         timer = read_timer;
      }
      if (timer) {
         timer->cancel();
      }
      notify_writes();
   }

   void notify_reads() {
      std::shared_ptr<boost::asio::steady_timer> timer;
      {
         auto lock = std::scoped_lock{mutex};
         timer = read_timer;
      }
      if (timer) {
         timer->cancel();
      }
   }

   void notify_writes() {
      std::shared_ptr<boost::asio::steady_timer> timer;
      {
         auto lock = std::scoped_lock{mutex};
         timer = write_timer;
      }
      if (timer) {
         timer->cancel();
      }
   }

   void push_read(bytes value) {
      {
         auto lock = std::scoped_lock{mutex};
         reads.push_back(std::move(value));
      }
      notify_reads();
   }

   void release_writes() {
      {
         auto lock = std::scoped_lock{mutex};
         writes_released = true;
      }
      notify_writes();
   }

   void hold_future_writes() {
      auto lock = std::scoped_lock{mutex};
      hold_writes = true;
      writes_released = false;
   }

   [[nodiscard]] std::size_t write_count() const {
      auto lock = std::scoped_lock{mutex};
      return writes.size();
   }

   [[nodiscard]] std::size_t blocked_write_count() const {
      auto lock = std::scoped_lock{mutex};
      return blocked_writes;
   }

   [[nodiscard]] std::uint64_t cancellation_count() const {
      auto lock = std::scoped_lock{mutex};
      return cancel_count;
   }

   [[nodiscard]] bytes written(std::size_t index) const {
      auto lock = std::scoped_lock{mutex};
      return writes.at(index);
   }

   std::int64_t id_value = 7;
   std::deque<bytes> reads;
   std::vector<bytes> writes;
   std::uint64_t cancel_count = 0;
   std::shared_ptr<boost::asio::steady_timer> read_timer;
   std::shared_ptr<boost::asio::steady_timer> write_timer;
   std::weak_ptr<fake_stream> peer;
   bool wait_for_reads = false;
   bool hold_writes = false;
   bool writes_released = false;
   std::size_t blocked_writes = 0;
   bool open = true;

 private:
   boost::asio::awaitable<void> wait_for_write_release() {
      const auto executor = co_await boost::asio::this_coro::executor;
      {
         auto lock = std::scoped_lock{mutex};
         if (!hold_writes || writes_released) {
            co_return;
         }
         ++blocked_writes;
         if (!write_timer) {
            write_timer = std::make_shared<boost::asio::steady_timer>(executor);
         }
      }

      while (true) {
         std::shared_ptr<boost::asio::steady_timer> timer;
         {
            auto lock = std::scoped_lock{mutex};
            if (!open) {
               FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, "fake stream closed");
            }
            if (!hold_writes || writes_released) {
               co_return;
            }
            timer = write_timer;
         }
         timer->expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
   }

   mutable std::mutex mutex;
};

class fake_session final : public forge::net::transport::detail::session_concept {
 public:
   [[nodiscard]] bool valid() const noexcept override {
      return open;
   }

   boost::asio::awaitable<forge::net::transport::stream> async_open_stream() override {
      FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, "fake session does not open outbound streams");
   }

   boost::asio::awaitable<forge::net::transport::stream> async_accept_stream() override {
      if (accepted.empty()) {
         open = false;
         FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, "fake session closed");
      }
      auto out = std::move(accepted.front());
      accepted.pop_front();
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      open = false;
      co_return;
   }

   void cancel() override {
      open = false;
   }

   std::deque<forge::net::transport::stream> accepted;
   bool open = true;
};

[[nodiscard]] forge::net::transport::stream make_stream(std::shared_ptr<fake_stream> model) {
   return forge::net::transport::detail::stream_access::make(std::move(model));
}

[[nodiscard]] std::pair<forge::net::transport::stream, forge::net::transport::stream>
make_stream_pair(const std::shared_ptr<fake_stream>& first, const std::shared_ptr<fake_stream>& second) {
   first->wait_for_reads = true;
   second->wait_for_reads = true;
   first->peer = second;
   second->peer = first;
   return {make_stream(first), make_stream(second)};
}

[[nodiscard]] forge::net::transport::session make_session(std::shared_ptr<fake_session> model) {
   return forge::net::transport::detail::session_access::make(std::move(model));
}

[[nodiscard]] bytes pack_api_frame(const forge::api::core::frame& frame) {
   auto payload = forge::api::core::bytes{};
   forge::raw::pack(payload, frame);
   return forge::net::transport::encode_frame(payload);
}

[[nodiscard]] bytes pack_api_frame_with_trailing_byte(const forge::api::core::frame& frame) {
   auto payload = forge::api::core::bytes{};
   forge::raw::pack(payload, frame);
   payload.push_back(0xffU);
   return forge::net::transport::encode_frame(payload);
}

[[nodiscard]] bytes pack_api_frame_with_oversized_api_id(forge::api::core::frame_kind kind, std::uint64_t id) {
   auto stream = forge::datastream<bytes>{};
   forge::raw::pack(stream, kind, forge::api::core::call_id{.value = id}, forge::unsigned_int{4096U});
   return forge::net::transport::encode_frame(stream.storage());
}

[[nodiscard]] forge::api::core::frame unpack_written_frame(const bytes& value) {
   const auto decoded = forge::net::transport::decode_frame(value);
   BOOST_REQUIRE(decoded.status == forge::net::transport::frame_decode_status::complete);
   return forge::raw::unpack<forge::api::core::frame>(decoded.payload);
}

[[nodiscard]] forge::api::core::frame read_request(std::uint64_t id, std::string ref) {
   return forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = id},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_payload(protocol::read_chunk{.ref = std::move(ref)}),
   };
}

[[nodiscard]] forge::api::core::frame read_response(std::uint64_t id, std::string value) {
   return forge::api::core::frame{
       .kind = forge::api::core::frame_kind::response,
       .id = {.value = id},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_payload(protocol::chunk{.bytes = std::move(value)}),
   };
}

[[nodiscard]] forge::api::core::frame stream_item(std::uint64_t id, std::string value) {
   auto item = read_response(id, std::move(value));
   item.kind = forge::api::core::frame_kind::stream_item;
   return item;
}

[[nodiscard]] forge::api::core::frame stream_end(std::uint64_t id) {
   auto end = read_response(id, "");
   end.kind = forge::api::core::frame_kind::stream_end;
   end.payload.clear();
   return end;
}

boost::asio::awaitable<void> wait_for_writes(const std::shared_ptr<fake_stream>& model, std::size_t count) {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto timer = boost::asio::steady_timer{executor};
   while (model->write_count() < count) {
      timer.expires_after(std::chrono::milliseconds{1});
      co_await timer.async_wait(boost::asio::use_awaitable);
   }
}

struct call_state {
   explicit call_state(boost::asio::any_io_executor executor_value) : timer(std::move(executor_value)) {
      timer.expires_at(boost::asio::steady_timer::time_point::max());
   }

   boost::asio::steady_timer timer;
   std::optional<forge::api::core::frame> response;
   std::exception_ptr error;
   bool done = false;
};

struct stream_call_state {
   explicit stream_call_state(boost::asio::any_io_executor executor_value) : timer(std::move(executor_value)) {
      timer.expires_at(boost::asio::steady_timer::time_point::max());
   }

   boost::asio::steady_timer timer;
   std::optional<std::vector<forge::api::core::frame>> response;
   std::exception_ptr error;
   bool done = false;
};

struct service_state {
   explicit service_state(boost::asio::any_io_executor executor_value) : timer(std::move(executor_value)) {
      timer.expires_at(boost::asio::steady_timer::time_point::max());
   }

   boost::asio::steady_timer timer;
   std::exception_ptr error;
   bool done = false;
};

std::shared_ptr<service_state> start_service(boost::asio::any_io_executor executor,
                                             boost::asio::awaitable<void> operation) {
   auto state = std::make_shared<service_state>(executor);
   boost::asio::co_spawn(
       executor,
       [operation = std::move(operation), state]() mutable -> boost::asio::awaitable<void> {
          try {
             co_await std::move(operation);
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.cancel();
       },
       boost::asio::detached);
   return state;
}

boost::asio::awaitable<void> wait_service(std::shared_ptr<service_state> state) {
   while (!state->done) {
      auto error = boost::system::error_code{};
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
}

template <typename Predicate>
boost::asio::awaitable<void> wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto timer = boost::asio::steady_timer{executor};
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (!predicate()) {
      BOOST_REQUIRE_MESSAGE(std::chrono::steady_clock::now() < deadline, "timed out waiting for async condition");
      timer.expires_after(std::chrono::milliseconds{1});
      co_await timer.async_wait(boost::asio::use_awaitable);
   }
}

std::shared_ptr<call_state> start_call(forge::api::transport::client& client, boost::asio::any_io_executor executor,
                                       forge::api::core::frame request) {
   auto state = std::make_shared<call_state>(executor);
   boost::asio::co_spawn(
       executor,
       [&client, request = std::move(request), state]() mutable -> boost::asio::awaitable<void> {
          try {
             state->response.emplace(co_await client.async_call(std::move(request)));
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.cancel();
       },
       boost::asio::detached);
   return state;
}

boost::asio::awaitable<forge::api::core::frame> wait_call(std::shared_ptr<call_state> state) {
   while (!state->done) {
      auto error = boost::system::error_code{};
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->response);
}

std::shared_ptr<stream_call_state> start_stream_call(forge::api::transport::client& client,
                                                     boost::asio::any_io_executor executor,
                                                     forge::api::core::frame request,
                                                     forge::api::transport::call_options options = {}) {
   auto state = std::make_shared<stream_call_state>(executor);
   boost::asio::co_spawn(
       executor,
       [&client, request = std::move(request), options = std::move(options),
        state]() mutable -> boost::asio::awaitable<void> {
          try {
             state->response.emplace(co_await client.async_call_stream(std::move(request), std::move(options)));
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.cancel();
       },
       boost::asio::detached);
   return state;
}

std::shared_ptr<stream_call_state>
start_cancellable_stream_call(forge::api::transport::client& client, boost::asio::any_io_executor executor,
                              forge::api::core::frame request,
                              const std::shared_ptr<boost::asio::cancellation_signal>& cancellation) {
   auto state = std::make_shared<stream_call_state>(executor);
   boost::asio::co_spawn(
       executor,
       [&client, request = std::move(request), state]() mutable -> boost::asio::awaitable<void> {
          try {
             state->response.emplace(co_await client.async_call_stream(std::move(request)));
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.cancel();
       },
       boost::asio::bind_cancellation_slot(cancellation->slot(), boost::asio::detached));
   return state;
}

boost::asio::awaitable<std::vector<forge::api::core::frame>>
wait_stream_call(std::shared_ptr<stream_call_state> state) {
   while (!state->done) {
      auto error = boost::system::error_code{};
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->response);
}

} // namespace

BOOST_AUTO_TEST_SUITE(transport_api_tests)

BOOST_AUTO_TEST_CASE(transport_api_client_roundtrips_frame_level_calls) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(forge::api::core::frame{
       .kind = forge::api::core::frame_kind::response,
       .id = {.value = 9},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_payload(protocol::chunk{.bytes = "abc:ok"}),
   }));

   auto client = forge::api::transport::client{make_stream(model), forge::api::transport::options{}};
   auto request = read_request(9, "abc");

   const auto response = forge::asio::blocking::run(runtime, client.async_call(std::move(request)));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "abc:ok");
   BOOST_REQUIRE_EQUAL(model->write_count(), 1U);
   BOOST_TEST(unpack_written_frame(model->written(0)).id.value == 9U);
}

BOOST_AUTO_TEST_CASE(transport_api_client_rejects_frame_trailing_bytes_as_protocol_error) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame_with_trailing_byte(read_response(1, "unexpected")));
   auto client = forge::api::transport::client{make_stream(model), forge::api::transport::options{}};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, client.async_call(read_request(1, "trailing"))),
                     forge::api::core::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(transport_api_client_rejects_frame_allocation_bomb_as_resource_exhausted) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(
       pack_api_frame_with_oversized_api_id(forge::api::core::frame_kind::response, std::uint64_t{1}));
   auto client = forge::api::transport::client{make_stream(model), forge::api::transport::options{}};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, client.async_call(read_request(1, "allocation"))),
                     forge::api::core::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(transport_api_client_routes_concurrent_out_of_order_responses) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = forge::api::transport::client{make_stream(model), forge::api::transport::options{}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_call(client, executor, read_request(1, "one"));
      auto second = start_call(client, executor, read_request(2, "two"));

      co_await wait_for_writes(model, 2);
      model->push_read(pack_api_frame(read_response(2, "two:ok")));
      model->push_read(pack_api_frame(read_response(1, "one:ok")));

      const auto first_response = co_await wait_call(std::move(first));
      const auto second_response = co_await wait_call(std::move(second));

      BOOST_TEST(forge::raw::unpack<protocol::chunk>(first_response.payload).bytes == "one:ok");
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(second_response.payload).bytes == "two:ok");
      BOOST_REQUIRE_EQUAL(model->write_count(), 2U);
      BOOST_TEST(unpack_written_frame(model->written(0)).id.value == 1U);
      BOOST_TEST(unpack_written_frame(model->written(1)).id.value == 2U);
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_serializes_concurrent_stream_calls) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 2}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_stream_call(client, executor, read_request(0, "one"));
      auto second = start_stream_call(client, executor, read_request(0, "two"));

      co_await wait_for_writes(model, 2);
      const auto first_id = unpack_written_frame(model->written(0)).id.value;
      const auto second_id = unpack_written_frame(model->written(1)).id.value;
      BOOST_TEST(first_id != 0U);
      BOOST_TEST(second_id != 0U);
      BOOST_TEST(first_id != second_id);

      model->push_read(pack_api_frame(stream_item(second_id, "two:0")));
      model->push_read(pack_api_frame(stream_end(second_id)));
      model->push_read(pack_api_frame(stream_item(first_id, "one:0")));
      model->push_read(pack_api_frame(stream_end(first_id)));

      const auto first_response = co_await wait_stream_call(std::move(first));
      const auto second_response = co_await wait_stream_call(std::move(second));

      BOOST_REQUIRE_GE(first_response.size(), 1U);
      BOOST_REQUIRE_GE(second_response.size(), 1U);
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(first_response.front().payload).bytes == "one:0");
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(second_response.front().payload).bytes == "two:0");
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_serializes_concurrent_max_inflight_rejection) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 1}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_stream_call(client, executor, read_request(0, "one"));
      co_await wait_for_writes(model, 1);

      auto rejected = false;
      try {
         (void)co_await client.async_call_stream(read_request(0, "two"));
      } catch (const forge::api::core::exceptions::resource_exhausted&) {
         rejected = true;
      }
      BOOST_TEST(rejected);

      const auto first_id = unpack_written_frame(model->written(0)).id.value;
      model->push_read(pack_api_frame(stream_item(first_id, "one:0")));
      model->push_read(pack_api_frame(stream_end(first_id)));
      const auto first_response = co_await wait_stream_call(std::move(first));
      BOOST_REQUIRE_GE(first_response.size(), 1U);
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(first_response.front().payload).bytes == "one:0");
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_deadline_expires_while_waiting_for_write_lock) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      model->hold_writes = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 2}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_stream_call(client, executor, read_request(0, "one"));
      co_await wait_until([&] { return model->blocked_write_count() == 1; }, std::chrono::milliseconds{100});

      auto second = start_stream_call(client, executor, read_request(0, "two"),
                                      forge::api::transport::call_options{.deadline = std::chrono::milliseconds{10}});

      auto timer = boost::asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{50});
      co_await timer.async_wait(boost::asio::use_awaitable);
      BOOST_TEST(second->done);

      client.cancel();
      model->release_writes();

      auto second_deadline = false;
      try {
         (void)co_await wait_stream_call(std::move(second));
      } catch (const forge::api::core::exceptions::deadline_exceeded&) {
         second_deadline = true;
      }
      BOOST_TEST(second_deadline);

      try {
         (void)co_await wait_stream_call(std::move(first));
      } catch (const forge::net::transport::exceptions::closed&) {
      } catch (const forge::api::core::exceptions::deadline_exceeded&) {
      } catch (const forge::api::core::exceptions::cancelled&) {
      }

      BOOST_TEST(model->write_count() == 0U);
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_deadline_interrupts_active_write_and_unblocks_writer_queue) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      model->hold_writes = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 2}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto active = start_stream_call(client, executor, read_request(1, "active"),
                                      forge::api::transport::call_options{.deadline = std::chrono::milliseconds{50}});
      co_await wait_until([&] { return model->blocked_write_count() == 1; }, std::chrono::milliseconds{100});
      auto queued = start_stream_call(client, executor, read_request(2, "queued"));

      co_await wait_until([&] { return active->done && queued->done; }, std::chrono::milliseconds{250});

      auto deadline = false;
      try {
         (void)co_await wait_stream_call(std::move(active));
      } catch (const forge::api::core::exceptions::deadline_exceeded&) {
         deadline = true;
      }
      BOOST_TEST(deadline);

      auto queued_cancelled = false;
      try {
         (void)co_await wait_stream_call(std::move(queued));
      } catch (const forge::api::core::exceptions::cancelled&) {
         queued_cancelled = true;
      }
      BOOST_TEST(queued_cancelled);
      BOOST_TEST(model->cancel_count == 1U);
      BOOST_TEST(model->write_count() == 0U);
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_caller_cancel_interrupts_active_write_and_unblocks_writer_queue) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      model->hold_writes = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 2}};
      const auto executor = co_await boost::asio::this_coro::executor;
      auto cancellation = std::make_shared<boost::asio::cancellation_signal>();

      auto active = start_cancellable_stream_call(client, executor, read_request(1, "active"), cancellation);
      co_await wait_until([&] { return model->blocked_write_count() == 1; }, std::chrono::milliseconds{100});
      auto queued = start_stream_call(client, executor, read_request(2, "queued"));
      auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{5}};
      co_await timer.async_wait(boost::asio::use_awaitable);
      cancellation->emit(boost::asio::cancellation_type::all);

      co_await wait_until([&] { return active->done && queued->done; }, std::chrono::milliseconds{250});

      auto active_cancelled = false;
      try {
         (void)co_await wait_stream_call(std::move(active));
      } catch (const forge::api::core::exceptions::cancelled&) {
         active_cancelled = true;
      }
      BOOST_TEST(active_cancelled);

      auto queued_cancelled = false;
      try {
         (void)co_await wait_stream_call(std::move(queued));
      } catch (const forge::api::core::exceptions::cancelled&) {
         queued_cancelled = true;
      }
      BOOST_TEST(queued_cancelled);
      BOOST_TEST(model->cancel_count == 1U);
      BOOST_TEST(model->write_count() == 0U);
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_keeps_streaming_call_pending_until_stream_end) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 1}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto pending = start_call(client, executor, read_request(3, "stream"));
      co_await wait_for_writes(model, 1);

      model->push_read(pack_api_frame(stream_item(3, "stream:0")));
      auto timer = boost::asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{10});
      co_await timer.async_wait(boost::asio::use_awaitable);
      BOOST_TEST(!pending->done);

      auto rejected = false;
      try {
         (void)co_await client.async_call(read_request(4, "blocked"));
      } catch (const forge::api::core::exceptions::resource_exhausted&) {
         rejected = true;
      }
      BOOST_TEST(rejected);

      model->push_read(pack_api_frame(stream_end(3)));
      const auto response = co_await wait_call(std::move(pending));
      BOOST_TEST(static_cast<int>(response.kind) == static_cast<int>(forge::api::core::frame_kind::stream_item));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "stream:0");
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_returns_streaming_response_sequence) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = forge::api::transport::client{make_stream(model), forge::api::transport::options{}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto pending = start_stream_call(client, executor, read_request(5, "stream"));
      co_await wait_for_writes(model, 1);
      model->push_read(pack_api_frame(stream_item(5, "stream:0")));
      model->push_read(pack_api_frame(stream_item(5, "stream:1")));
      model->push_read(pack_api_frame(stream_end(5)));

      const auto responses = co_await wait_stream_call(std::move(pending));
      BOOST_REQUIRE_EQUAL(responses.size(), 3U);
      BOOST_TEST(static_cast<int>(responses[0].kind) == static_cast<int>(forge::api::core::frame_kind::stream_item));
      BOOST_TEST(static_cast<int>(responses[1].kind) == static_cast<int>(forge::api::core::frame_kind::stream_item));
      BOOST_TEST(static_cast<int>(responses[2].kind) == static_cast<int>(forge::api::core::frame_kind::stream_end));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[0].payload).bytes == "stream:0");
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[1].payload).bytes == "stream:1");
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_releases_streaming_slot_after_stream_end) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 1}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_stream_call(client, executor, read_request(6, "first"));
      co_await wait_for_writes(model, 1);
      model->push_read(pack_api_frame(stream_item(6, "first:0")));
      model->push_read(pack_api_frame(stream_end(6)));
      const auto first_response = co_await wait_stream_call(std::move(first));
      BOOST_REQUIRE_EQUAL(first_response.size(), 2U);

      auto second = start_stream_call(client, executor, read_request(7, "second"));
      co_await wait_for_writes(model, 2);
      model->push_read(pack_api_frame(stream_item(7, "second:0")));
      model->push_read(pack_api_frame(stream_end(7)));
      const auto second_response = co_await wait_stream_call(std::move(second));
      BOOST_REQUIRE_EQUAL(second_response.size(), 2U);
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(second_response[0].payload).bytes == "second:0");
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_enforces_max_inflight) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client =
          forge::api::transport::client{make_stream(model), forge::api::transport::options{.max_inflight = 1}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_call(client, executor, read_request(1, "one"));
      co_await wait_for_writes(model, 1);

      auto rejected = false;
      try {
         (void)co_await client.async_call(read_request(2, "two"));
      } catch (const forge::api::core::exceptions::resource_exhausted&) {
         rejected = true;
      }
      BOOST_TEST(rejected);

      model->push_read(pack_api_frame(read_response(1, "one:ok")));
      const auto first_response = co_await wait_call(std::move(first));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(first_response.payload).bytes == "one:ok");
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_deadline_cancels_pending_call) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = forge::api::transport::client{
          make_stream(model), forge::api::transport::options{.deadline = std::chrono::milliseconds{10}}};
      auto deadline = false;
      try {
         (void)co_await client.async_call(read_request(1, "late"));
      } catch (const forge::api::core::exceptions::deadline_exceeded&) {
         deadline = true;
      }
      BOOST_TEST(deadline);
      co_await wait_for_writes(model, 2);
      const auto cancel = unpack_written_frame(model->written(1));
      BOOST_TEST(static_cast<int>(cancel.kind) == static_cast<int>(forge::api::core::frame_kind::cancel));
      BOOST_TEST(cancel.id.value == 1U);
      BOOST_TEST(model->cancel_count == 0U);

      auto reused_call_id = false;
      try {
         (void)co_await client.async_call(read_request(1, "reused"));
      } catch (const forge::api::core::exceptions::protocol_error&) {
         reused_call_id = true;
      }
      BOOST_TEST(reused_call_id);

      const auto executor = co_await boost::asio::this_coro::executor;
      auto next = start_call(client, executor, read_request(2, "next"));
      co_await wait_for_writes(model, 3);
      model->push_read(pack_api_frame(read_response(2, "next:ok")));
      const auto response = co_await wait_call(std::move(next));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "next:ok");
      client.cancel();
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_client_cancel_unblocks_pending_call) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = forge::api::transport::client{make_stream(model), forge::api::transport::options{}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto pending = start_call(client, executor, read_request(1, "cancel"));
      co_await wait_for_writes(model, 1);
      client.cancel();

      auto cancelled = false;
      try {
         (void)co_await wait_call(std::move(pending));
      } catch (const forge::api::core::exceptions::cancelled&) {
         cancelled = true;
      }
      BOOST_TEST(cancelled);
      BOOST_TEST(model->cancel_count == 1U);
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_cancel_control_write_has_a_bounded_deadline) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = forge::api::transport::client{
          make_stream(model), forge::api::transport::options{.control_timeout = std::chrono::milliseconds{10}}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto cancellation = std::make_shared<boost::asio::cancellation_signal>();
      auto pending = start_cancellable_stream_call(client, executor, read_request(1, "wait"), cancellation);
      co_await wait_for_writes(model, 1);
      model->hold_future_writes();
      cancellation->emit(boost::asio::cancellation_type::all);

      auto cancelled = false;
      try {
         (void)co_await wait_stream_call(std::move(pending));
      } catch (const forge::api::core::exceptions::cancelled&) {
         cancelled = true;
      }
      BOOST_TEST(cancelled);
      co_await wait_until([model] { return model->blocked_write_count() == 1U; }, std::chrono::milliseconds{100});
      co_await wait_until([model] { return model->cancellation_count() == 1U; }, std::chrono::milliseconds{250});
      BOOST_TEST(!client.valid());
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_queued_cancel_timeout_cancels_unrelated_pending_calls) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = forge::api::transport::client{
          make_stream(model), forge::api::transport::options{.control_timeout = std::chrono::milliseconds{10}}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto cancellation = std::make_shared<boost::asio::cancellation_signal>();
      auto retired = start_cancellable_stream_call(client, executor, read_request(1, "retire"), cancellation);
      co_await wait_for_writes(model, 1);

      model->hold_future_writes();
      auto unrelated = start_call(client, executor, read_request(2, "unrelated"));
      co_await wait_until([model] { return model->blocked_write_count() == 1U; }, std::chrono::milliseconds{100});
      cancellation->emit(boost::asio::cancellation_type::all);

      auto retired_cancelled = false;
      try {
         (void)co_await wait_stream_call(std::move(retired));
      } catch (const forge::api::core::exceptions::cancelled&) {
         retired_cancelled = true;
      }
      BOOST_TEST(retired_cancelled);

      auto unrelated_cancelled = false;
      try {
         (void)co_await wait_call(std::move(unrelated));
      } catch (const forge::api::core::exceptions::cancelled&) {
         unrelated_cancelled = true;
      }
      BOOST_TEST(unrelated_cancelled);
      BOOST_TEST(!client.valid());
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_caller_cancel_reaches_owner_and_preserves_connection) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto client_model = std::make_shared<fake_stream>();
      auto server_model = std::make_shared<fake_stream>();
      auto [client_stream, server_stream] = make_stream_pair(client_model, server_model);

      auto owner_state = std::make_shared<cancellable_state>();
      auto registry = forge::api::core::registry{};
      registry.install<cache_api>(cache_api::describe(), std::make_shared<cancellable_cache_impl>(owner_state));
      auto plan = forge::api::core::binding().serve(registry).build();
      auto service = start_service(
          executor, forge::api::transport::serve_stream(std::move(server_stream), std::move(plan),
                                                        forge::api::transport::options{.max_inflight = 1}));
      auto client =
          forge::api::transport::client{std::move(client_stream), forge::api::transport::options{.max_inflight = 1}};

      auto cancellation = std::make_shared<boost::asio::cancellation_signal>();
      auto pending = std::make_shared<call_state>(executor);
      boost::asio::co_spawn(
          executor,
          [&client, pending]() mutable -> boost::asio::awaitable<void> {
             try {
                pending->response.emplace(co_await client.async_call(read_request(1, "wait")));
             } catch (...) {
                pending->error = std::current_exception();
             }
             pending->done = true;
             pending->timer.cancel();
          },
          boost::asio::bind_cancellation_slot(cancellation->slot(), boost::asio::detached));

      co_await wait_until([owner_state] { return owner_state->started.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      cancellation->emit(boost::asio::cancellation_type::all);

      auto caller_cancelled = false;
      try {
         (void)co_await wait_call(pending);
      } catch (const forge::api::core::exceptions::cancelled&) {
         caller_cancelled = true;
      }
      BOOST_TEST(caller_cancelled);
      co_await wait_until([owner_state] { return owner_state->cancelled.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      co_await wait_for_writes(client_model, 2);
      const auto cancel = unpack_written_frame(client_model->written(1));
      BOOST_TEST(static_cast<int>(cancel.kind) == static_cast<int>(forge::api::core::frame_kind::cancel));
      BOOST_TEST(cancel.id.value == 1U);
      co_await wait_for_writes(server_model, 1);
      const auto acknowledgement = unpack_written_frame(server_model->written(0));
      BOOST_TEST(static_cast<int>(acknowledgement.kind) == static_cast<int>(forge::api::core::frame_kind::cancel));
      BOOST_TEST(acknowledgement.id.value == 1U);

      const auto next = co_await client.async_call(read_request(2, "next"));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(next.payload).bytes == "next:ok");

      owner_state->started.store(false, std::memory_order_release);
      owner_state->cancelled.store(false, std::memory_order_release);
      auto second_cancellation = std::make_shared<boost::asio::cancellation_signal>();
      auto second_pending = std::make_shared<call_state>(executor);
      boost::asio::co_spawn(
          executor,
          [&client, second_pending]() mutable -> boost::asio::awaitable<void> {
             try {
                second_pending->response.emplace(co_await client.async_call(read_request(3, "wait")));
             } catch (...) {
                second_pending->error = std::current_exception();
             }
             second_pending->done = true;
             second_pending->timer.cancel();
          },
          boost::asio::bind_cancellation_slot(second_cancellation->slot(), boost::asio::detached));
      co_await wait_until([owner_state] { return owner_state->started.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      second_cancellation->emit(boost::asio::cancellation_type::all);
      caller_cancelled = false;
      try {
         (void)co_await wait_call(second_pending);
      } catch (const forge::api::core::exceptions::cancelled&) {
         caller_cancelled = true;
      }
      BOOST_TEST(caller_cancelled);
      co_await wait_until([owner_state] { return owner_state->cancelled.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      co_await wait_for_writes(server_model, 3);
      const auto second_acknowledgement = unpack_written_frame(server_model->written(2));
      BOOST_TEST(static_cast<int>(second_acknowledgement.kind) ==
                 static_cast<int>(forge::api::core::frame_kind::cancel));
      BOOST_TEST(second_acknowledgement.id.value == 3U);

      const auto final = co_await client.async_call(read_request(4, "final"));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(final.payload).bytes == "final:ok");

      client.cancel();
      server_model->cancel();
      co_await wait_service(std::move(service));
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_late_cancelled_generation_cannot_complete_reused_call_id) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto client_model = std::make_shared<fake_stream>();
      auto server_model = std::make_shared<fake_stream>();
      auto [client_stream, server_stream] = make_stream_pair(client_model, server_model);

      auto owner_state = std::make_shared<generation_state>();
      auto registry = forge::api::core::registry{};
      registry.install<cache_api>(cache_api::describe(), std::make_shared<generation_cache_impl>(owner_state));
      auto service = start_service(
          executor,
          forge::api::transport::serve_stream(
              std::move(server_stream), forge::api::core::binding().serve(registry).build(),
              forge::api::transport::options{.max_inflight = 2, .disconnect_grace = std::chrono::milliseconds{50}}));
      auto client =
          forge::api::transport::client{std::move(client_stream), forge::api::transport::options{.max_inflight = 2}};

      auto cancellation = std::make_shared<boost::asio::cancellation_signal>();
      auto old = std::make_shared<call_state>(executor);
      boost::asio::co_spawn(
          executor,
          [&client, old]() mutable -> boost::asio::awaitable<void> {
             try {
                old->response.emplace(co_await client.async_call(read_request(1, "old")));
             } catch (...) {
                old->error = std::current_exception();
             }
             old->done = true;
             old->timer.cancel();
          },
          boost::asio::bind_cancellation_slot(cancellation->slot(), boost::asio::detached));
      co_await wait_until([owner_state] { return owner_state->old_started.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      cancellation->emit(boost::asio::cancellation_type::all);
      try {
         (void)co_await wait_call(old);
      } catch (const forge::api::core::exceptions::cancelled&) {
      }
      co_await wait_until([owner_state] { return owner_state->old_cancelled.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      co_await wait_for_writes(server_model, 1);

      auto replacement = start_call(client, executor, read_request(1, "new"));
      co_await wait_until([owner_state] { return owner_state->new_started.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      owner_state->release_old.store(true, std::memory_order_release);
      co_await wait_until([owner_state] { return owner_state->old_finished.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      owner_state->release_new.store(true, std::memory_order_release);

      const auto response = co_await wait_call(std::move(replacement));
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "new:ok");

      client.cancel();
      server_model->cancel();
      co_await wait_service(std::move(service));
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_server_deadline_cancels_owner_and_preserves_connection) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto client_model = std::make_shared<fake_stream>();
      auto server_model = std::make_shared<fake_stream>();
      auto [client_stream, server_stream] = make_stream_pair(client_model, server_model);

      auto owner_state = std::make_shared<cancellable_state>();
      auto registry = forge::api::core::registry{};
      registry.install<cache_api>(cache_api::describe(), std::make_shared<cancellable_cache_impl>(owner_state));
      auto plan = forge::api::core::binding().serve(registry).build();
      auto service = start_service(
          executor, forge::api::transport::serve_stream(
                        std::move(server_stream), std::move(plan),
                        forge::api::transport::options{.max_inflight = 1, .deadline = std::chrono::milliseconds{10}}));
      auto connection = forge::api::transport::connection{std::move(client_stream), forge::api::transport::options{}};
      auto cache = co_await connection.get_remote_api<cache_api>();

      auto deadline = false;
      try {
         (void)co_await cache->read(protocol::read_chunk{.ref = "wait"});
      } catch (const forge::api::core::exceptions::deadline_exceeded&) {
         deadline = true;
      }
      BOOST_TEST(deadline);
      co_await wait_until([owner_state] { return owner_state->cancelled.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});

      const auto response = co_await cache->read(protocol::read_chunk{.ref = "next"});
      BOOST_TEST(response.bytes == "next:ok");

      connection.cancel();
      server_model->cancel();
      co_await wait_service(std::move(service));
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_clean_disconnect_bounds_owner_lifetime) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto client_model = std::make_shared<fake_stream>();
      auto server_model = std::make_shared<fake_stream>();
      auto [client_stream, server_stream] = make_stream_pair(client_model, server_model);

      auto owner_state = std::make_shared<cancellable_state>();
      auto registry = forge::api::core::registry{};
      registry.install<cache_api>(cache_api::describe(), std::make_shared<cancellable_cache_impl>(owner_state));
      auto plan = forge::api::core::binding().serve(registry).build();
      auto service = start_service(
          executor, forge::api::transport::serve_stream(
                        std::move(server_stream), std::move(plan),
                        forge::api::transport::options{.disconnect_grace = std::chrono::milliseconds{10}}));
      auto client = forge::api::transport::client{std::move(client_stream), forge::api::transport::options{}};
      auto pending = start_call(client, executor, read_request(1, "wait"));
      co_await wait_until([owner_state] { return owner_state->started.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});

      client.cancel();
      server_model->cancel();
      co_await wait_until([owner_state] { return owner_state->cancelled.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      co_await wait_service(std::move(service));

      try {
         (void)co_await wait_call(std::move(pending));
      } catch (const forge::api::core::exceptions::cancelled&) {
      } catch (const forge::net::transport::exceptions::closed&) {
      }
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_disconnect_grace_retains_binding_until_cancelled_owner_finishes) {
   auto runtime = forge::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto client_model = std::make_shared<fake_stream>();
      auto server_model = std::make_shared<fake_stream>();
      auto [client_stream, server_stream] = make_stream_pair(client_model, server_model);
      auto owner_state = std::make_shared<delayed_cancellation_state>();

      auto owned_service = [owner_state,
                            server_stream = std::move(server_stream)]() mutable -> boost::asio::awaitable<void> {
         auto binding_owner = std::make_shared<forge::api::core::registry>();
         auto api_owner = std::make_shared<delayed_cancellation_cache_impl>(owner_state);
         owner_state->binding_owner = binding_owner;
         owner_state->api_owner = api_owner;
         binding_owner->install<cache_api>(cache_api::describe(), std::move(api_owner));
         auto plan = forge::api::core::binding().serve(*binding_owner).build();
         co_await forge::api::transport::serve_stream(
             std::move(server_stream), std::move(plan),
             forge::api::transport::options{.disconnect_grace = std::chrono::milliseconds{10}});
      };

      auto service = start_service(executor, owned_service());
      auto client = forge::api::transport::client{std::move(client_stream), forge::api::transport::options{}};
      auto pending = start_call(client, executor, read_request(1, "wait"));
      co_await wait_until([owner_state] { return owner_state->started.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});

      client.cancel();
      server_model->cancel();
      co_await wait_until([owner_state] { return owner_state->cancelled.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});

      auto settle = boost::asio::steady_timer{executor, std::chrono::milliseconds{25}};
      co_await settle.async_wait(boost::asio::use_awaitable);
      BOOST_TEST(!service->done);
      BOOST_TEST(!owner_state->finished.load(std::memory_order_acquire));
      BOOST_TEST(!owner_state->binding_owner.expired());
      BOOST_TEST(!owner_state->api_owner.expired());

      owner_state->release.store(true, std::memory_order_release);
      co_await wait_until([owner_state] { return owner_state->finished.load(std::memory_order_acquire); },
                          std::chrono::milliseconds{250});
      co_await wait_service(std::move(service));
      co_await wait_until(
          [owner_state] { return owner_state->binding_owner.expired() && owner_state->api_owner.expired(); },
          std::chrono::milliseconds{250});

      try {
         (void)co_await wait_call(std::move(pending));
      } catch (const forge::api::core::exceptions::cancelled&) {
      } catch (const forge::net::transport::exceptions::closed&) {
      }
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_connection_returns_typed_remote_handle) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(forge::api::core::frame{
       .kind = forge::api::core::frame_kind::response,
       .id = {.value = 1},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = forge::api::core::pack_body(transport_api_typed::chunk{.bytes = "typed:ok"}),
   }));

   auto scenario = [model]() -> boost::asio::awaitable<void> {
      auto connection = forge::api::transport::connection{make_stream(model), forge::api::transport::options{}};
      auto cache = co_await connection.get_remote_api<transport_api_typed::cache_api>();
      const auto response = co_await cache->read(transport_api_typed::read_chunk{.ref = "typed"});

      BOOST_TEST(response.bytes == "typed:ok");
   };

   forge::asio::blocking::run(runtime, scenario());
   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto request = unpack_written_frame(model->writes.front());
   BOOST_TEST(request.method == "read");
   BOOST_TEST(forge::api::core::unpack_body<transport_api_typed::read_chunk>(request.payload).ref == "typed");
}

BOOST_AUTO_TEST_CASE(transport_api_connection_returns_positional_remote_handle) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(forge::api::core::frame{
       .kind = forge::api::core::frame_kind::response,
       .id = {.value = 1},
       .api = {.id = {"positional.transport"}, .major = 1, .min_revision = 0},
       .method = "join",
       .codec = {.value = "forge.raw"},
       .payload = forge::api::core::pack_body(transport_api_typed::chunk{.bytes = "left:right:remote"}),
   }));

   auto scenario = [model]() -> boost::asio::awaitable<void> {
      auto connection = forge::api::transport::connection{make_stream(model), forge::api::transport::options{}};
      auto positional = co_await connection.get_remote_api<transport_api_typed::positional_api>();
      const auto response = co_await positional->join("left", "right");

      BOOST_TEST(response.bytes == "left:right:remote");
   };

   forge::asio::blocking::run(runtime, scenario());
   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto request = unpack_written_frame(model->writes.front());
   BOOST_TEST(request.api.id.value == "positional.transport");
   BOOST_TEST(request.method == "join");
   const auto args = forge::api::core::unpack_body<std::tuple<std::string, std::string>>(request.payload);
   BOOST_TEST(std::get<0>(args) == "left");
   BOOST_TEST(std::get<1>(args) == "right");
}

BOOST_AUTO_TEST_CASE(connection_get_remote_api_preserves_requested_revision) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(forge::api::core::frame{
       .kind = forge::api::core::frame_kind::response,
       .id = {.value = 1},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 2},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = forge::api::core::pack_body(transport_api_typed::chunk{.bytes = "typed:older"}),
   }));

   auto scenario = [model]() -> boost::asio::awaitable<void> {
      auto connection = forge::api::transport::connection{make_stream(model), forge::api::transport::options{}};
      auto cache =
          co_await connection.get_remote_api<transport_api_typed::cache_api>(transport_api_typed::cache_api::ref(2));
      const auto response = co_await cache->read(transport_api_typed::read_chunk{.ref = "typed"});

      BOOST_TEST(response.bytes == "typed:older");
   };

   forge::asio::blocking::run(runtime, scenario());
   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto request = unpack_written_frame(model->writes.front());
   BOOST_TEST(request.api.id.value == "cache");
   BOOST_TEST(request.api.major == 1U);
   BOOST_TEST(request.api.min_revision == 2U);
   BOOST_TEST(request.method == "read");
}

BOOST_AUTO_TEST_CASE(transport_api_serve_stream_dispatches_requests) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(read_request(11, "server")));

   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = forge::api::core::binding().serve(registry).build();

   forge::asio::blocking::run(runtime, forge::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                           forge::api::transport::options{}));

   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto response = unpack_written_frame(model->writes.front());
   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "server:ok");
}

BOOST_AUTO_TEST_CASE(transport_api_serve_stream_dispatches_positional_requests) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 15},
       .api = {.id = {"positional.transport"}, .major = 1, .min_revision = 0},
       .method = "join",
       .codec = {.value = "forge.raw"},
       .payload = forge::api::core::pack_body(std::make_tuple(std::string{"server"}, std::string{"args"})),
   }));

   auto registry = forge::api::core::registry{};
   registry.install<positional_api>(positional_api::describe(), std::make_shared<positional_impl>());
   auto plan = forge::api::core::binding().serve(registry).build();

   forge::asio::blocking::run(runtime, forge::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                           forge::api::transport::options{}));

   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto response = unpack_written_frame(model->writes.front());
   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "server:args:ok");
}

BOOST_AUTO_TEST_CASE(transport_api_serve_stream_overwrites_reserved_metadata_with_trusted_values) {
   auto runtime = forge::asio::runtime{};
   auto request = read_request(13, "context");
   request.meta.push_back(
       {.key = std::string{forge::api::core::p2p_remote_peer_metadata_key}, .value = "spoofed-peer"});

   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(request));

   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto observed_peer = std::make_shared<std::string>();
   auto observed_payload = std::make_shared<std::string>();
   auto plan =
       forge::api::core::binding()
           .serve(registry)
           .interceptor(forge::api::core::interceptor()
                            .id("trusted-peer")
                            .phase(forge::api::core::interceptor_phase::authorize)
                            .handler([observed_peer, observed_payload](
                                         forge::api::core::call_context& context) -> boost::asio::awaitable<void> {
                               *observed_peer = forge::api::core::metadata_value(
                                                    context.meta, forge::api::core::p2p_remote_peer_metadata_key)
                                                    .value_or("no-remote-peer");
                               *observed_payload = forge::raw::unpack<protocol::read_chunk>(context.payload).ref;
                               co_return;
                            })
                            .build())
           .build();

   forge::asio::blocking::run(
       runtime, forge::api::transport::serve_stream(
                    make_stream(model), std::move(plan), forge::api::transport::options{},
                    forge::api::core::metadata{{.key = std::string{forge::api::core::p2p_remote_peer_metadata_key},
                                                .value = "trusted-peer"}}));

   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto response = unpack_written_frame(model->writes.front());
   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "context:ok");
   BOOST_TEST(*observed_peer == "trusted-peer");
   BOOST_TEST(*observed_payload == "context");
}

BOOST_AUTO_TEST_CASE(transport_api_serve_stream_without_trusted_peer_has_no_remote_peer_context) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(read_request(14, "context")));

   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto observed_peer = std::make_shared<std::string>();
   auto plan =
       forge::api::core::binding()
           .serve(registry)
           .interceptor(
               forge::api::core::interceptor()
                   .id("trusted-peer")
                   .phase(forge::api::core::interceptor_phase::authorize)
                   .handler([observed_peer](forge::api::core::call_context& context) -> boost::asio::awaitable<void> {
                      *observed_peer =
                          forge::api::core::metadata_value(context.meta, forge::api::core::p2p_remote_peer_metadata_key)
                              .value_or("no-remote-peer");
                      co_return;
                   })
                   .build())
           .build();

   forge::asio::blocking::run(runtime, forge::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                           forge::api::transport::options{}));

   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto response = unpack_written_frame(model->writes.front());
   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "context:ok");
   BOOST_TEST(*observed_peer == "no-remote-peer");
}

BOOST_AUTO_TEST_CASE(transport_api_serve_session_accepts_streams) {
   auto runtime = forge::asio::runtime{};
   auto stream_model = std::make_shared<fake_stream>();
   stream_model->reads.push_back(pack_api_frame(read_request(12, "session")));
   auto session_model = std::make_shared<fake_session>();
   session_model->accepted.push_back(make_stream(stream_model));

   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = forge::api::core::binding().serve(registry).build();

   forge::asio::blocking::run(runtime,
                              forge::api::transport::serve_session(make_session(session_model), std::move(plan),
                                                                   forge::api::transport::session_options{}));

   BOOST_REQUIRE_EQUAL(stream_model->writes.size(), 1U);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(unpack_written_frame(stream_model->writes.front()).payload).bytes ==
              "session:ok");
}

BOOST_AUTO_TEST_CASE(transport_api_serve_session_serializes_admission_on_multi_worker_runtime) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};

   auto scenario = []() -> boost::asio::awaitable<void> {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto state = std::make_shared<gated_state>();
      auto first = std::make_shared<fake_stream>();
      first->reads.push_back(pack_api_frame(read_request(21, "first")));
      auto second = std::make_shared<fake_stream>();
      second->reads.push_back(pack_api_frame(read_request(22, "second")));
      auto session_model = std::make_shared<fake_session>();
      session_model->accepted.push_back(make_stream(first));
      session_model->accepted.push_back(make_stream(second));

      auto registry = forge::api::core::registry{};
      registry.install<cache_api>(cache_api::describe(), std::make_shared<gated_cache_impl>(state));
      auto plan = forge::api::core::binding().serve(registry).build();
      auto service = start_service(executor, forge::api::transport::serve_session(
                                                 make_session(session_model), std::move(plan),
                                                 forge::api::transport::session_options{.max_concurrent_streams = 1}));

      co_await wait_until(
          [state] {
             auto lock = std::scoped_lock{state->mutex};
             return state->first_started == 1;
          },
          std::chrono::milliseconds{250});

      auto timer = boost::asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{25});
      co_await timer.async_wait(boost::asio::use_awaitable);

      {
         auto lock = std::scoped_lock{state->mutex};
         BOOST_TEST(state->second_started == 0U);
         BOOST_TEST(state->max_active == 1U);
      }

      {
         auto lock = std::scoped_lock{state->mutex};
         state->release_first = true;
      }

      co_await wait_service(std::move(service));

      BOOST_REQUIRE_EQUAL(first->writes.size(), 1U);
      BOOST_REQUIRE_EQUAL(second->writes.size(), 1U);
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(unpack_written_frame(first->writes.front()).payload).bytes ==
                 "first:ok");
      BOOST_TEST(forge::raw::unpack<protocol::chunk>(unpack_written_frame(second->writes.front()).payload).bytes ==
                 "second:ok");
      {
         auto lock = std::scoped_lock{state->mutex};
         BOOST_TEST(state->max_active == 1U);
      }
   };

   forge::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(transport_api_serve_stream_rejects_frame_trailing_bytes_as_protocol_error) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame_with_trailing_byte(read_request(31, "trailing")));
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = forge::api::core::binding().serve(registry).build();

   BOOST_CHECK_THROW(
       forge::asio::blocking::run(runtime, forge::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                               forge::api::transport::options{})),
       forge::api::core::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(transport_api_serve_stream_rejects_frame_allocation_bomb_as_resource_exhausted) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(
       pack_api_frame_with_oversized_api_id(forge::api::core::frame_kind::request, std::uint64_t{32}));
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = forge::api::core::binding().serve(registry).build();

   BOOST_CHECK_THROW(
       forge::asio::blocking::run(runtime, forge::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                               forge::api::transport::options{})),
       forge::api::core::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(transport_api_rejects_codec_mismatch_as_typed_error) {
   auto runtime = forge::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   auto bad = read_request(13, "bad");
   bad.codec.value = "other";
   model->reads.push_back(pack_api_frame(bad));

   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = forge::api::core::binding().serve(registry).build();

   BOOST_CHECK_THROW(
       forge::asio::blocking::run(runtime, forge::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                               forge::api::transport::options{})),
       forge::api::core::exceptions::codec_failed);
}

BOOST_AUTO_TEST_SUITE_END()
