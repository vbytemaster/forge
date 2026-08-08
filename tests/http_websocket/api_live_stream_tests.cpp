#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/api/http/macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

import forge.api.core.binding;
import forge.api.core.duplex_stream;
import forge.api.core.exceptions;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.core.stream_reader;
import forge.api.core.stream_writer;
import forge.api.http.binding;
import forge.api.http.parameters;
import forge.api.http.proxy;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.net.http.base_url;
import forge.net.http.client;
import forge.net.http.exceptions;
import forge.net.http.router;
import forge.net.http.server;
import forge.net.http.types;
import forge.raw.raw;

namespace forge::api::http::live_test {

struct item {
   std::uint32_t value = 0;
   bool operator==(const item&) const = default;
};

struct total {
   std::uint32_t value = 0;
   bool operator==(const total&) const = default;
};

struct metadata {
   std::string value;
};

template <typename Stream>
Stream& operator<<(Stream& stream, const item& value) {
   forge::raw::pack(stream, value.value);
   return stream;
}

template <typename Stream>
Stream& operator>>(Stream& stream, item& value) {
   forge::raw::unpack(stream, value.value);
   return stream;
}

template <typename Stream>
Stream& operator<<(Stream& stream, const total& value) {
   forge::raw::pack(stream, value.value);
   return stream;
}

template <typename Stream>
Stream& operator>>(Stream& stream, total& value) {
   forge::raw::unpack(stream, value.value);
   return stream;
}

template <typename Stream>
Stream& operator<<(Stream& stream, const metadata& value) {
   forge::raw::pack(stream, value.value);
   return stream;
}

template <typename Stream>
Stream& operator>>(Stream& stream, metadata& value) {
   forge::raw::unpack(stream, value.value);
   return stream;
}

class live_api
    : public forge::api::core::contract<
         live_api, forge::api::core::surface::local |
                      forge::api::core::surface::remote> {
 public:
   virtual ~live_api() = default;

   virtual boost::asio::awaitable<void>
   download(std::string ref, std::uint32_t count,
            forge::api::core::stream_writer<item> output) = 0;
   virtual boost::asio::awaitable<total>
   upload(std::string ref, forge::api::core::stream_reader<item> input) = 0;
};

class bidirectional_api
    : public forge::api::core::contract<
         bidirectional_api, forge::api::core::surface::local |
                               forge::api::core::surface::remote> {
 public:
   virtual ~bidirectional_api() = default;

   virtual boost::asio::awaitable<void>
   exchange(forge::api::core::duplex_stream<item, item> stream) = 0;
};

class conflicting_api
    : public forge::api::core::contract<
         conflicting_api, forge::api::core::surface::local |
                            forge::api::core::surface::remote> {
 public:
   virtual ~conflicting_api() = default;

   virtual boost::asio::awaitable<total>
   upload(metadata fixed, forge::api::core::stream_reader<item> input) = 0;
};

class route_conflicting_api
    : public forge::api::core::contract<
         route_conflicting_api, forge::api::core::surface::local |
                                  forge::api::core::surface::remote> {
 public:
   virtual ~route_conflicting_api() = default;

   virtual boost::asio::awaitable<total>
   upload(forge::api::http::header<std::string> tenant,
          forge::api::core::stream_reader<item> input) = 0;
};

} // namespace forge::api::http::live_test

BOOST_DESCRIBE_STRUCT(::forge::api::http::live_test::metadata, (), (value))

FORGE_API(::forge::api::http::live_test::live_api,
          FORGE_API_CONTRACT("http.live", 1, 0),
          FORGE_API_METHOD(download, ref, count),
          FORGE_API_METHOD(upload, ref))

FORGE_API(::forge::api::http::live_test::bidirectional_api,
          FORGE_API_CONTRACT("http.live.bidirectional", 1, 0),
          FORGE_API_METHOD(exchange))

FORGE_API(::forge::api::http::live_test::conflicting_api,
          FORGE_API_CONTRACT("http.live.conflicting", 1, 0),
          FORGE_API_METHOD(upload, fixed))

FORGE_API(::forge::api::http::live_test::route_conflicting_api,
          FORGE_API_CONTRACT("http.live.route-conflicting", 1, 0),
          FORGE_API_METHOD(upload, tenant))

FORGE_HTTP_API(
   ::forge::api::http::live_test::live_api,
   FORGE_HTTP_GET(download, "/live/:ref?count={count}"),
   FORGE_HTTP_POST(upload, "/live/:ref", ok))

FORGE_HTTP_API(
   ::forge::api::http::live_test::bidirectional_api,
   FORGE_HTTP_POST(exchange, "/live/exchange", ok))

FORGE_HTTP_API(
   ::forge::api::http::live_test::conflicting_api,
   FORGE_HTTP_POST(upload, "/live/conflicting", ok))

FORGE_HTTP_API(
   ::forge::api::http::live_test::route_conflicting_api,
   FORGE_HTTP_POST(upload, "/live/:tenant", ok))

namespace forge::api::http::live_test {
namespace {

class live_impl final : public live_api {
 public:
   boost::asio::awaitable<void>
   download(std::string ref, std::uint32_t count,
            forge::api::core::stream_writer<item> output) override {
      if (ref == "wait-cancel" || ref == "close-wait-cancel") {
         if (ref == "close-wait-cancel") {
            co_await output.async_close();
            wait_download_output_closed.store(true, std::memory_order_release);
         }
         wait_download_started.store(true, std::memory_order_release);
         const auto executor = co_await boost::asio::this_coro::executor;
         auto delay = boost::asio::steady_timer{executor};
         while (!release_wait_download.load(std::memory_order_acquire)) {
            const auto cancellation =
               co_await boost::asio::this_coro::cancellation_state;
            if (cancellation.cancelled() !=
                boost::asio::cancellation_type::none) {
               download_cancelled.store(true, std::memory_order_release);
               throw forge::api::core::exceptions::cancelled{
                  "HTTP stream handler was cancelled"};
            }
            delay.expires_after(std::chrono::milliseconds{1});
            auto error = boost::system::error_code{};
            co_await delay.async_wait(boost::asio::redirect_error(
               boost::asio::use_awaitable, error));
         }
         co_await output.async_close();
         co_return;
      }
      for (auto index = std::uint32_t{0}; index != count; ++index) {
         co_await output.async_write(item{.value = index + 1U});
         produced.store(index + 1U, std::memory_order_release);
         if (ref == "fail" && index == 0U) {
            throw std::runtime_error{"download failed"};
         }
      }
      co_await output.async_close();
   }

   boost::asio::awaitable<total>
   upload(std::string ref,
          forge::api::core::stream_reader<item> input) override {
      auto result = std::uint32_t{static_cast<std::uint32_t>(ref.size())};
      while (auto value = co_await input.async_read()) {
         result += value->value;
      }
      co_return total{.value = result};
   }

   std::atomic_uint32_t produced{0};
   std::atomic_bool wait_download_started{false};
   std::atomic_bool wait_download_output_closed{false};
   std::atomic_bool download_cancelled{false};
   std::atomic_bool release_wait_download{false};
};

template <typename Predicate>
boost::asio::awaitable<bool>
wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto delay = boost::asio::steady_timer{executor};
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
         co_return false;
      }
      delay.expires_after(std::chrono::milliseconds{1});
      co_await delay.async_wait(boost::asio::use_awaitable);
   }
   co_return true;
}

BOOST_AUTO_TEST_SUITE(api_http_live_stream_suite)

BOOST_AUTO_TEST_CASE(server_and_client_streams_use_incremental_http_bodies) {
   auto runtime = forge::asio::runtime{
      forge::asio::runtime_options{.worker_threads = 2}};
   auto implementation = std::make_shared<live_impl>();
   auto registry = forge::api::core::registry{};
   registry.install<live_api>(live_api::describe(), implementation);

   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                   .use(forge::api::core::binding().serve(registry).build())
                   .bind<live_api>()
                   .build());
   auto server = forge::net::http::server{
      runtime, forge::net::http::server_config{}, std::move(router)};
   server.start();

   auto client = forge::net::http::client{
      runtime, forge::net::http::parse_base_url(
                  "http://127.0.0.1:" + std::to_string(server.port()))};
   auto remote = forge::asio::blocking::run(
      runtime, forge::api::http::remote<live_api>(client));

   auto scenario = [&]() -> boost::asio::awaitable<void> {
      auto download = co_await remote.async_open<&live_api::download>("asset", 64U);
      BOOST_TEST(implementation->produced.load(std::memory_order_acquire) < 64U);
      for (auto expected = std::uint32_t{1}; expected <= 64U; ++expected) {
         const auto value = co_await download.async_read();
         BOOST_REQUIRE(value.has_value());
         BOOST_TEST(value->value == expected);
      }
      BOOST_TEST(!(co_await download.async_read()).has_value());
      co_await download.async_finish();

      auto upload = co_await remote.async_open<&live_api::upload>("abc");
      co_await upload.async_write(item{.value = 4});
      co_await upload.async_write(item{.value = 7});
      co_await upload.async_close();
      BOOST_TEST((co_await upload.async_finish()).value == 14U);
   };
   forge::asio::blocking::run(runtime, scenario());
   server.stop();
}

BOOST_AUTO_TEST_CASE(server_stream_reports_terminal_failure_after_delivered_items) {
   auto runtime = forge::asio::runtime{
      forge::asio::runtime_options{.worker_threads = 2}};
   auto registry = forge::api::core::registry{};
   registry.install<live_api>(live_api::describe(), std::make_shared<live_impl>());
   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                   .use(forge::api::core::binding().serve(registry).build())
                   .bind<live_api>()
                   .build());
   auto server = forge::net::http::server{
      runtime, forge::net::http::server_config{}, std::move(router)};
   server.start();
   auto client = forge::net::http::client{
      runtime, forge::net::http::parse_base_url(
                  "http://127.0.0.1:" + std::to_string(server.port()))};
   auto remote = forge::asio::blocking::run(
      runtime, forge::api::http::remote<live_api>(client));

   auto scenario = [&]() -> boost::asio::awaitable<void> {
      auto download = co_await remote.async_open<&live_api::download>("fail", 3U);
      const auto first = co_await download.async_read();
      BOOST_REQUIRE(first.has_value());
      BOOST_TEST(first->value == 1U);
      BOOST_TEST(!(co_await download.async_read()).has_value());
      auto failed = false;
      try {
         co_await download.async_finish();
      } catch (const forge::api::core::exceptions::remote_internal&) {
         failed = true;
      }
      BOOST_TEST(failed);
   };
   forge::asio::blocking::run(runtime, scenario());
   server.stop();
}

BOOST_AUTO_TEST_CASE(server_stream_interceptor_failure_closes_output) {
   auto runtime = forge::asio::runtime{
      forge::asio::runtime_options{.worker_threads = 2}};
   auto registry = forge::api::core::registry{};
   registry.install<live_api>(live_api::describe(),
                              std::make_shared<live_impl>());
   auto plan = forge::api::core::binding()
                  .serve(registry)
                  .interceptor(
                     forge::api::core::interceptor()
                        .id("reject")
                        .phase(
                           forge::api::core::interceptor_phase::authorize)
                        .handler([](forge::api::core::call_context&)
                                    -> boost::asio::awaitable<void> {
                           throw std::runtime_error{"request rejected"};
                        })
                        .build())
                  .build();
   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                   .use(std::move(plan))
                   .bind<live_api>()
                   .build());
   auto server = forge::net::http::server{
      runtime, forge::net::http::server_config{}, std::move(router)};
   server.start();
   auto client = forge::net::http::client{
      runtime, forge::net::http::parse_base_url(
                  "http://127.0.0.1:" + std::to_string(server.port()))};
   auto remote = forge::asio::blocking::run(
      runtime, forge::api::http::remote<live_api>(client));

   auto scenario = [&]() -> boost::asio::awaitable<void> {
      auto download = co_await remote.async_open<&live_api::download>(
         forge::api::core::call_options{
            .deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{2}},
         "asset", 1U);
      auto ended = false;
      try {
         ended = !(co_await download.async_read()).has_value();
      } catch (...) {
      }
      BOOST_TEST(ended);

      auto failed = false;
      try {
         co_await download.async_finish();
      } catch (const forge::api::core::exceptions::remote_internal&) {
         failed = true;
      }
      BOOST_TEST(failed);
   };
   forge::asio::blocking::run(runtime, scenario());
   server.stop();
}

BOOST_AUTO_TEST_CASE(server_stream_abandonment_cancels_handler) {
   auto runtime = forge::asio::runtime{
      forge::asio::runtime_options{.worker_threads = 2}};
   auto implementation = std::make_shared<live_impl>();
   auto registry = forge::api::core::registry{};
   registry.install<live_api>(live_api::describe(), implementation);
   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                   .use(forge::api::core::binding()
                           .serve(registry)
                           .build())
                   .bind<live_api>()
                   .build());
   auto server = forge::net::http::server{
      runtime, forge::net::http::server_config{}, std::move(router)};
   server.start();
   auto client = forge::net::http::client{
      runtime, forge::net::http::parse_base_url(
                  "http://127.0.0.1:" + std::to_string(server.port()))};
   auto remote = forge::asio::blocking::run(
      runtime, forge::api::http::remote<live_api>(client));

   auto scenario = [&]() -> boost::asio::awaitable<void> {
      auto download = co_await remote.async_open<&live_api::download>(
         "wait-cancel", 1U);
      BOOST_REQUIRE(co_await wait_until(
         [implementation] {
            return implementation->wait_download_started.load(
               std::memory_order_acquire);
         },
         std::chrono::seconds{2}));

      download.cancel();
      auto cancelled = false;
      try {
         co_await download.async_finish();
      } catch (const forge::api::core::exceptions::cancelled&) {
         cancelled = true;
      }
      BOOST_TEST(cancelled);

      const auto observed = co_await wait_until(
         [implementation] {
            return implementation->download_cancelled.load(
               std::memory_order_acquire);
         },
         std::chrono::seconds{2});
      if (!observed) {
         implementation->release_wait_download.store(
            true, std::memory_order_release);
      }
      BOOST_TEST(observed);
   };
   forge::asio::blocking::run(runtime, scenario());
   server.stop();
}

BOOST_AUTO_TEST_CASE(server_stream_abandonment_after_stream_end_cancels_handler) {
   auto runtime = forge::asio::runtime{
      forge::asio::runtime_options{.worker_threads = 2}};
   auto implementation = std::make_shared<live_impl>();
   auto registry = forge::api::core::registry{};
   registry.install<live_api>(live_api::describe(), implementation);
   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                   .use(forge::api::core::binding()
                           .serve(registry)
                           .build())
                   .bind<live_api>()
                   .build());
   auto server = forge::net::http::server{
      runtime, forge::net::http::server_config{}, std::move(router)};
   server.start();
   auto client = forge::net::http::client{
      runtime, forge::net::http::parse_base_url(
                  "http://127.0.0.1:" + std::to_string(server.port()))};
   auto remote = forge::asio::blocking::run(
      runtime, forge::api::http::remote<live_api>(client));

   auto scenario = [&]() -> boost::asio::awaitable<void> {
      auto download = co_await remote.async_open<&live_api::download>(
         "close-wait-cancel", 1U);
      BOOST_TEST(!(co_await download.async_read()).has_value());
      BOOST_REQUIRE(implementation->wait_download_output_closed.load(
         std::memory_order_acquire));

      download.cancel();
      auto cancelled = false;
      try {
         co_await download.async_finish();
      } catch (const forge::api::core::exceptions::cancelled&) {
         cancelled = true;
      }
      BOOST_TEST(cancelled);

      const auto observed = co_await wait_until(
         [implementation] {
            return implementation->download_cancelled.load(
               std::memory_order_acquire);
         },
         std::chrono::seconds{2});
      if (!observed) {
         implementation->release_wait_download.store(
            true, std::memory_order_release);
      }
      BOOST_TEST(observed);
   };
   forge::asio::blocking::run(runtime, scenario());
   server.stop();
}

BOOST_AUTO_TEST_CASE(http_rejects_bidirectional_and_client_stream_body_mappings) {
   auto router = forge::net::http::router{};
   auto bidirectional = forge::api::http::binding()
                           .bind<bidirectional_api>()
                           .build();
   BOOST_CHECK_THROW(router.mount(bidirectional),
                     forge::api::core::exceptions::incompatible_version);

   auto conflicting = forge::api::http::binding()
                         .bind<conflicting_api>()
                         .build();
   BOOST_CHECK_THROW(router.mount(conflicting),
                     forge::net::http::exceptions::bad_request);

   auto route_conflicting = forge::api::http::binding()
                               .bind<route_conflicting_api>()
                               .build();
   BOOST_CHECK_THROW(router.mount(route_conflicting),
                     forge::net::http::exceptions::bad_request);

   auto runtime = forge::asio::runtime{};
   auto client = forge::net::http::client{
      runtime, forge::net::http::parse_base_url("http://127.0.0.1:9")};
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime, forge::api::http::remote<bidirectional_api>(client)),
      forge::api::core::exceptions::incompatible_version);
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime, forge::api::http::remote<conflicting_api>(client)),
      forge::net::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace
} // namespace forge::api::http::live_test
