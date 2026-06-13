#include <boost/test/unit_test.hpp>
#include <fcl/api/api_macros.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.api.transport.exceptions;
import fcl.api.transport.options;
import fcl.api.transport.client;
import fcl.api.transport.connection;
import fcl.api.transport.server;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.raw.raw;
import fcl.transport.exceptions;
import fcl.transport.session;
import fcl.transport.stream;

namespace api_transport_typed {

struct read_chunk {
   std::string ref;
};

struct chunk {
   std::string bytes;
};

template <typename Stream> Stream& operator<<(Stream& stream, const read_chunk& value) {
   fcl::raw::pack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, read_chunk& value) {
   fcl::raw::unpack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator<<(Stream& stream, const chunk& value) {
   fcl::raw::pack(stream, value.bytes);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, chunk& value) {
   fcl::raw::unpack(stream, value.bytes);
   return stream;
}

class cache_api
    : public fcl::api::contract<cache_api, fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~cache_api() = default;
   virtual boost::asio::awaitable<chunk> read(read_chunk request) = 0;
};

} // namespace api_transport_typed

FCL_API(::api_transport_typed::cache_api, FCL_API_CONTRACT("cache", 1, 8), FCL_API_METHOD(read))

namespace {

using bytes = std::vector<std::uint8_t>;
namespace protocol = api_transport_typed;
using cache_api = api_transport_typed::cache_api;

template <typename T>
[[nodiscard]] fcl::api::bytes pack_payload(const T& value) {
   auto out = fcl::api::bytes{};
   fcl::raw::pack(out, value);
   return out;
}

class cache_impl final : public cache_api {
 public:
   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      co_return protocol::chunk{.bytes = request.ref + ":ok"};
   }
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

class fake_stream final : public fcl::transport::detail::stream_concept {
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
      auto lock = std::scoped_lock{mutex};
      writes.push_back({value.begin(), value.end()});
      co_return;
   }

   boost::asio::awaitable<void> async_write_chunk(fcl::transport::chunk value) override {
      co_await wait_for_write_release();
      auto lock = std::scoped_lock{mutex};
      writes.push_back(value.to_vector());
      co_return;
   }

   boost::asio::awaitable<bytes> async_read() override {
      co_return (co_await async_read_chunk()).into_vector();
   }

   boost::asio::awaitable<fcl::transport::chunk> async_read_chunk() override {
      if (wait_for_reads) {
         {
            auto lock = std::scoped_lock{mutex};
            if (!reads.empty()) {
               auto out = std::move(reads.front());
               reads.pop_front();
               co_return fcl::transport::chunk{std::move(out)};
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
         FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "fake stream closed");
      }
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return fcl::transport::chunk{std::move(out)};
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

   [[nodiscard]] std::size_t write_count() const {
      auto lock = std::scoped_lock{mutex};
      return writes.size();
   }

   [[nodiscard]] std::size_t blocked_write_count() const {
      auto lock = std::scoped_lock{mutex};
      return blocked_writes;
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
               FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "fake stream closed");
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

class fake_session final : public fcl::transport::detail::session_concept {
 public:
   [[nodiscard]] bool valid() const noexcept override {
      return open;
   }

   boost::asio::awaitable<fcl::transport::stream> async_open_stream() override {
      FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "fake session does not open outbound streams");
   }

   boost::asio::awaitable<fcl::transport::stream> async_accept_stream() override {
      if (accepted.empty()) {
         open = false;
         FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "fake session closed");
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

   std::deque<fcl::transport::stream> accepted;
   bool open = true;
};

[[nodiscard]] fcl::transport::stream make_stream(std::shared_ptr<fake_stream> model) {
   return fcl::transport::detail::stream_access::make(std::move(model));
}

[[nodiscard]] fcl::transport::session make_session(std::shared_ptr<fake_session> model) {
   return fcl::transport::detail::session_access::make(std::move(model));
}

[[nodiscard]] bytes pack_api_frame(const fcl::api::frame& frame) {
   auto payload = fcl::api::bytes{};
   fcl::raw::pack(payload, frame);
   return fcl::transport::encode_frame(payload);
}

[[nodiscard]] fcl::api::frame unpack_written_frame(const bytes& value) {
   const auto decoded = fcl::transport::decode_frame(value);
   BOOST_REQUIRE(decoded.status == fcl::transport::frame_decode_status::complete);
   return fcl::raw::unpack<fcl::api::frame>(decoded.payload);
}

[[nodiscard]] fcl::api::frame read_request(std::uint64_t id, std::string ref) {
   return fcl::api::frame{
       .kind = fcl::api::frame_kind::request,
       .id = {.value = id},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "fcl.raw"},
       .payload = pack_payload(protocol::read_chunk{.ref = std::move(ref)}),
   };
}

[[nodiscard]] fcl::api::frame read_response(std::uint64_t id, std::string value) {
   return fcl::api::frame{
       .kind = fcl::api::frame_kind::response,
       .id = {.value = id},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "fcl.raw"},
       .payload = pack_payload(protocol::chunk{.bytes = std::move(value)}),
   };
}

[[nodiscard]] fcl::api::frame stream_item(std::uint64_t id, std::string value) {
   auto item = read_response(id, std::move(value));
   item.kind = fcl::api::frame_kind::stream_item;
   return item;
}

[[nodiscard]] fcl::api::frame stream_end(std::uint64_t id) {
   auto end = read_response(id, "");
   end.kind = fcl::api::frame_kind::stream_end;
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
   std::optional<fcl::api::frame> response;
   std::exception_ptr error;
   bool done = false;
};

struct stream_call_state {
   explicit stream_call_state(boost::asio::any_io_executor executor_value) : timer(std::move(executor_value)) {
      timer.expires_at(boost::asio::steady_timer::time_point::max());
   }

   boost::asio::steady_timer timer;
   std::optional<std::vector<fcl::api::frame>> response;
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

std::shared_ptr<call_state> start_call(fcl::api::transport::client& client, boost::asio::any_io_executor executor,
                                       fcl::api::frame request) {
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

boost::asio::awaitable<fcl::api::frame> wait_call(std::shared_ptr<call_state> state) {
   while (!state->done) {
      auto error = boost::system::error_code{};
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->response);
}

std::shared_ptr<stream_call_state> start_stream_call(fcl::api::transport::client& client,
                                                     boost::asio::any_io_executor executor,
                                                     fcl::api::frame request,
                                                     fcl::api::transport::call_options options = {}) {
   auto state = std::make_shared<stream_call_state>(executor);
   boost::asio::co_spawn(
       executor,
       [&client, request = std::move(request), options = std::move(options), state]() mutable
           -> boost::asio::awaitable<void> {
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

boost::asio::awaitable<std::vector<fcl::api::frame>> wait_stream_call(std::shared_ptr<stream_call_state> state) {
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

BOOST_AUTO_TEST_SUITE(api_transport_tests)

BOOST_AUTO_TEST_CASE(api_transport_client_roundtrips_frame_level_calls) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(fcl::api::frame{
       .kind = fcl::api::frame_kind::response,
       .id = {.value = 9},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "fcl.raw"},
       .payload = pack_payload(protocol::chunk{.bytes = "abc:ok"}),
   }));

   auto client = fcl::api::transport::client{make_stream(model), fcl::api::transport::options{}};
   auto request = read_request(9, "abc");

   const auto response = fcl::asio::blocking::run(runtime, client.async_call(std::move(request)));

   BOOST_CHECK(response.kind == fcl::api::frame_kind::response);
   BOOST_TEST(fcl::raw::unpack<protocol::chunk>(response.payload).bytes == "abc:ok");
   BOOST_REQUIRE_EQUAL(model->write_count(), 1U);
   BOOST_TEST(unpack_written_frame(model->written(0)).id.value == 9U);
}

BOOST_AUTO_TEST_CASE(api_transport_client_routes_concurrent_out_of_order_responses) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model), fcl::api::transport::options{}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_call(client, executor, read_request(1, "one"));
      auto second = start_call(client, executor, read_request(2, "two"));

      co_await wait_for_writes(model, 2);
      model->push_read(pack_api_frame(read_response(2, "two:ok")));
      model->push_read(pack_api_frame(read_response(1, "one:ok")));

      const auto first_response = co_await wait_call(std::move(first));
      const auto second_response = co_await wait_call(std::move(second));

      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(first_response.payload).bytes == "one:ok");
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(second_response.payload).bytes == "two:ok");
      BOOST_REQUIRE_EQUAL(model->write_count(), 2U);
      BOOST_TEST(unpack_written_frame(model->written(0)).id.value == 1U);
      BOOST_TEST(unpack_written_frame(model->written(1)).id.value == 2U);
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_serializes_concurrent_stream_calls) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model),
                                                fcl::api::transport::options{.max_inflight = 2}};
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
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(first_response.front().payload).bytes == "one:0");
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(second_response.front().payload).bytes == "two:0");
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_serializes_concurrent_max_inflight_rejection) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model),
                                                fcl::api::transport::options{.max_inflight = 1}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_stream_call(client, executor, read_request(0, "one"));
      co_await wait_for_writes(model, 1);

      auto rejected = false;
      try {
         (void)co_await client.async_call_stream(read_request(0, "two"));
      } catch (const fcl::api::exceptions::resource_exhausted&) {
         rejected = true;
      }
      BOOST_TEST(rejected);

      const auto first_id = unpack_written_frame(model->written(0)).id.value;
      model->push_read(pack_api_frame(stream_item(first_id, "one:0")));
      model->push_read(pack_api_frame(stream_end(first_id)));
      const auto first_response = co_await wait_stream_call(std::move(first));
      BOOST_REQUIRE_GE(first_response.size(), 1U);
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(first_response.front().payload).bytes == "one:0");
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_deadline_expires_while_waiting_for_write_lock) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      model->hold_writes = true;
      auto client = fcl::api::transport::client{make_stream(model),
                                                fcl::api::transport::options{.max_inflight = 2}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_stream_call(client, executor, read_request(0, "one"));
      co_await wait_until([&] { return model->blocked_write_count() == 1; }, std::chrono::milliseconds{100});

      auto second = start_stream_call(
          client, executor, read_request(0, "two"),
          fcl::api::transport::call_options{.deadline = std::chrono::milliseconds{10}});

      auto timer = boost::asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{50});
      co_await timer.async_wait(boost::asio::use_awaitable);
      BOOST_TEST(second->done);

      model->release_writes();

      auto second_deadline = false;
      try {
         (void)co_await wait_stream_call(std::move(second));
      } catch (const fcl::api::exceptions::deadline_exceeded&) {
         second_deadline = true;
      }
      BOOST_TEST(second_deadline);

      try {
         (void)co_await wait_stream_call(std::move(first));
      } catch (const fcl::transport::exceptions::closed&) {
      } catch (const fcl::api::exceptions::deadline_exceeded&) {
      } catch (const fcl::api::exceptions::cancelled&) {
      }

      BOOST_TEST(model->write_count() == 0U);
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_keeps_streaming_call_pending_until_stream_end) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model),
                                                fcl::api::transport::options{.max_inflight = 1}};
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
      } catch (const fcl::api::exceptions::resource_exhausted&) {
         rejected = true;
      }
      BOOST_TEST(rejected);

      model->push_read(pack_api_frame(stream_end(3)));
      const auto response = co_await wait_call(std::move(pending));
      BOOST_TEST(static_cast<int>(response.kind) == static_cast<int>(fcl::api::frame_kind::stream_item));
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(response.payload).bytes == "stream:0");
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_returns_streaming_response_sequence) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model), fcl::api::transport::options{}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto pending = start_stream_call(client, executor, read_request(5, "stream"));
      co_await wait_for_writes(model, 1);
      model->push_read(pack_api_frame(stream_item(5, "stream:0")));
      model->push_read(pack_api_frame(stream_item(5, "stream:1")));
      model->push_read(pack_api_frame(stream_end(5)));

      const auto responses = co_await wait_stream_call(std::move(pending));
      BOOST_REQUIRE_EQUAL(responses.size(), 3U);
      BOOST_TEST(static_cast<int>(responses[0].kind) == static_cast<int>(fcl::api::frame_kind::stream_item));
      BOOST_TEST(static_cast<int>(responses[1].kind) == static_cast<int>(fcl::api::frame_kind::stream_item));
      BOOST_TEST(static_cast<int>(responses[2].kind) == static_cast<int>(fcl::api::frame_kind::stream_end));
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(responses[0].payload).bytes == "stream:0");
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(responses[1].payload).bytes == "stream:1");
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_releases_streaming_slot_after_stream_end) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model),
                                                fcl::api::transport::options{.max_inflight = 1}};
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
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(second_response[0].payload).bytes == "second:0");
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_enforces_max_inflight) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model),
                                                fcl::api::transport::options{.max_inflight = 1}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto first = start_call(client, executor, read_request(1, "one"));
      co_await wait_for_writes(model, 1);

      auto rejected = false;
      try {
         (void)co_await client.async_call(read_request(2, "two"));
      } catch (const fcl::api::exceptions::resource_exhausted&) {
         rejected = true;
      }
      BOOST_TEST(rejected);

      model->push_read(pack_api_frame(read_response(1, "one:ok")));
      const auto first_response = co_await wait_call(std::move(first));
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(first_response.payload).bytes == "one:ok");
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_client_deadline_cancels_pending_call) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->wait_for_reads = true;
   auto client = fcl::api::transport::client{
       make_stream(model), fcl::api::transport::options{.deadline = std::chrono::milliseconds{10}}};

   BOOST_CHECK_THROW((void)fcl::asio::blocking::run(runtime, client.async_call(read_request(1, "late"))),
                     fcl::api::exceptions::deadline_exceeded);
   BOOST_TEST(model->cancel_count == 1U);
}

BOOST_AUTO_TEST_CASE(api_transport_client_cancel_unblocks_pending_call) {
   auto runtime = fcl::asio::runtime{};

   auto scenario = []() -> boost::asio::awaitable<void> {
      auto model = std::make_shared<fake_stream>();
      model->wait_for_reads = true;
      auto client = fcl::api::transport::client{make_stream(model), fcl::api::transport::options{}};
      const auto executor = co_await boost::asio::this_coro::executor;

      auto pending = start_call(client, executor, read_request(1, "cancel"));
      co_await wait_for_writes(model, 1);
      client.cancel();

      auto cancelled = false;
      try {
         (void)co_await wait_call(std::move(pending));
      } catch (const fcl::api::exceptions::cancelled&) {
         cancelled = true;
      }
      BOOST_TEST(cancelled);
      BOOST_TEST(model->cancel_count == 1U);
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_connection_returns_typed_remote_handle) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(fcl::api::frame{
       .kind = fcl::api::frame_kind::response,
       .id = {.value = 1},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "read",
       .codec = {.value = "fcl.raw"},
       .payload = fcl::api::pack_body(api_transport_typed::chunk{.bytes = "typed:ok"}),
   }));

   auto scenario = [model]() -> boost::asio::awaitable<void> {
      auto connection = fcl::api::transport::connection{make_stream(model), fcl::api::transport::options{}};
      auto cache = co_await connection.get_remote_api<api_transport_typed::cache_api>();
      const auto response = co_await cache->read(api_transport_typed::read_chunk{.ref = "typed"});

      BOOST_TEST(response.bytes == "typed:ok");
   };

   fcl::asio::blocking::run(runtime, scenario());
   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto request = unpack_written_frame(model->writes.front());
   BOOST_TEST(request.method == "read");
   BOOST_TEST(fcl::api::unpack_body<api_transport_typed::read_chunk>(request.payload).ref == "typed");
}

BOOST_AUTO_TEST_CASE(connection_get_remote_api_preserves_requested_revision) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(fcl::api::frame{
       .kind = fcl::api::frame_kind::response,
       .id = {.value = 1},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 2},
       .method = "read",
       .codec = {.value = "fcl.raw"},
       .payload = fcl::api::pack_body(api_transport_typed::chunk{.bytes = "typed:older"}),
   }));

   auto scenario = [model]() -> boost::asio::awaitable<void> {
      auto connection = fcl::api::transport::connection{make_stream(model), fcl::api::transport::options{}};
      auto cache = co_await connection.get_remote_api<api_transport_typed::cache_api>(
         api_transport_typed::cache_api::ref(2));
      const auto response = co_await cache->read(api_transport_typed::read_chunk{.ref = "typed"});

      BOOST_TEST(response.bytes == "typed:older");
   };

   fcl::asio::blocking::run(runtime, scenario());
   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto request = unpack_written_frame(model->writes.front());
   BOOST_TEST(request.api.id.value == "cache");
   BOOST_TEST(request.api.major == 1U);
   BOOST_TEST(request.api.min_revision == 2U);
   BOOST_TEST(request.method == "read");
}

BOOST_AUTO_TEST_CASE(api_transport_serve_stream_dispatches_requests) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   model->reads.push_back(pack_api_frame(read_request(11, "server")));

   auto registry = fcl::api::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = fcl::api::binding().serve(registry).build();

   fcl::asio::blocking::run(runtime, fcl::api::transport::serve_stream(make_stream(model), std::move(plan),
                                                                       fcl::api::transport::options{}));

   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   const auto response = unpack_written_frame(model->writes.front());
   BOOST_CHECK(response.kind == fcl::api::frame_kind::response);
   BOOST_TEST(fcl::raw::unpack<protocol::chunk>(response.payload).bytes == "server:ok");
}

BOOST_AUTO_TEST_CASE(api_transport_serve_session_accepts_streams) {
   auto runtime = fcl::asio::runtime{};
   auto stream_model = std::make_shared<fake_stream>();
   stream_model->reads.push_back(pack_api_frame(read_request(12, "session")));
   auto session_model = std::make_shared<fake_session>();
   session_model->accepted.push_back(make_stream(stream_model));

   auto registry = fcl::api::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = fcl::api::binding().serve(registry).build();

   fcl::asio::blocking::run(runtime, fcl::api::transport::serve_session(make_session(session_model), std::move(plan),
                                                                        fcl::api::transport::session_options{}));

   BOOST_REQUIRE_EQUAL(stream_model->writes.size(), 1U);
   BOOST_TEST(fcl::raw::unpack<protocol::chunk>(unpack_written_frame(stream_model->writes.front()).payload).bytes ==
              "session:ok");
}

BOOST_AUTO_TEST_CASE(api_transport_serve_session_serializes_admission_on_multi_worker_runtime) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};

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

      auto registry = fcl::api::registry{};
      registry.install<cache_api>(cache_api::describe(), std::make_shared<gated_cache_impl>(state));
      auto plan = fcl::api::binding().serve(registry).build();
      auto service =
          start_service(executor, fcl::api::transport::serve_session(
                                      make_session(session_model), std::move(plan),
                                      fcl::api::transport::session_options{.max_concurrent_streams = 1}));

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
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(unpack_written_frame(first->writes.front()).payload).bytes ==
                 "first:ok");
      BOOST_TEST(fcl::raw::unpack<protocol::chunk>(unpack_written_frame(second->writes.front()).payload).bytes ==
                 "second:ok");
      {
         auto lock = std::scoped_lock{state->mutex};
         BOOST_TEST(state->max_active == 1U);
      }
   };

   fcl::asio::blocking::run(runtime, scenario());
}

BOOST_AUTO_TEST_CASE(api_transport_rejects_codec_mismatch_as_typed_error) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>();
   auto bad = read_request(13, "bad");
   bad.codec.value = "other";
   model->reads.push_back(pack_api_frame(bad));

   auto registry = fcl::api::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   auto plan = fcl::api::binding().serve(registry).build();

   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, fcl::api::transport::serve_stream(make_stream(model),
                                                                                        std::move(plan),
                                                                                        fcl::api::transport::options{})),
                     fcl::api::exceptions::codec_failed);
}

BOOST_AUTO_TEST_SUITE_END()
