module;

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <boost/system/error_code.hpp>

module forge.net.p2p.node;

import forge.asio.runtime;
import forge.asio.notification;
import forge.multiformats.multihash;
import forge.multiformats.types;
import forge.net.p2p.dht;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;

#include "../../libraries/net/p2p/details/dht_routing_refresh.hxx"
#include "../../libraries/net/p2p/details/cancellation_latch.hxx"
#include "../../libraries/net/p2p/details/lifecycle_wakeup.hxx"

namespace forge::net::p2p {
namespace {

void cancel_timer_noexcept(const std::shared_ptr<boost::asio::steady_timer>& timer) noexcept {
   try {
      static_cast<void>(timer->cancel());
   } catch (...) {
   }
}

[[nodiscard]] peer_id refresh_peer(std::uint8_t value) {
   const auto payload = forge::multiformats::bytes{value};
   return peer_id::from_bytes(forge::multiformats::multihash::identity(payload).encode());
}

template <typename Predicate> [[nodiscard]] bool refresh_eventually(Predicate&& predicate) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
   while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
         return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
   }
   return true;
}

struct fake_refresh_clock {
   using time_point = detail::dht_routing_refresh::time_point;
   using duration = time_point::duration;
   using rep = duration::rep;

   std::atomic<rep> ticks{std::chrono::duration_cast<duration>(std::chrono::milliseconds{1}).count()};

   void set(time_point value) noexcept {
      ticks.store(value.time_since_epoch().count(), std::memory_order_release);
   }

   [[nodiscard]] time_point now() const noexcept {
      return time_point{duration{ticks.load(std::memory_order_acquire)}};
   }

   void advance(std::chrono::milliseconds amount) noexcept {
      ticks.fetch_add(std::chrono::duration_cast<duration>(amount).count(), std::memory_order_acq_rel);
   }

   [[nodiscard]] detail::dht_routing_refresh::time_source source() {
      return detail::dht_routing_refresh::time_source{
          .now = [this] { return now(); },
          .wait_until = [](std::shared_ptr<detail::lifecycle_wakeup> wakeup, std::uint64_t observed, time_point)
              -> boost::asio::awaitable<std::uint64_t> { co_return co_await wakeup->async_wait(observed); },
      };
   }
};

} // namespace

BOOST_AUTO_TEST_SUITE(dht_routing_refresh_tests)

BOOST_AUTO_TEST_CASE(dht_routing_refresh_uses_fake_time_and_coalesces_early_wakeups) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(1);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(2)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto queries = std::atomic_size_t{};
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::minutes{10},
           .query_timeout = std::chrono::milliseconds{75},
       }},
       [&queries](protocol_id, dht::key, std::chrono::milliseconds timeout,
                  std::shared_ptr<cancellation_latch>) -> boost::asio::awaitable<bool> {
          BOOST_TEST(timeout == std::chrono::milliseconds{75});
          queries.fetch_add(1, std::memory_order_acq_rel);
          co_return true;
       },
       clock.source(),
   };
   auto running = boost::asio::co_spawn(runtime.context(), refresh.async_run(), boost::asio::use_future);

   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > 1; }));
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   const auto initial = queries.load(std::memory_order_acquire);
   for (auto index = 0; index < 8; ++index) {
      refresh.notify_verified_server();
   }
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   BOOST_TEST(queries.load(std::memory_order_acquire) == initial);

   clock.advance(std::chrono::minutes{11});
   refresh.notify_verified_server();
   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > initial; }));

   refresh.request_stop();
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   running.get();
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_fake_time_proves_retry_backoff_and_cancellation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(11);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(12)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto queries = std::atomic_size_t{};
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::minutes{10},
           .query_timeout = std::chrono::milliseconds{125},
       }},
       [&queries](protocol_id, dht::key, std::chrono::milliseconds timeout,
                  std::shared_ptr<cancellation_latch>) -> boost::asio::awaitable<bool> {
          BOOST_TEST(timeout == std::chrono::milliseconds{125});
          queries.fetch_add(1, std::memory_order_acq_rel);
          co_return false;
       },
       clock.source(),
   };
   auto running = boost::asio::co_spawn(runtime.context(), refresh.async_run(), boost::asio::use_future);

   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > 1; }));
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   const auto failed = queries.load(std::memory_order_acquire);
   clock.advance(std::chrono::milliseconds{500});
   refresh.notify_verified_server();
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   BOOST_TEST(queries.load(std::memory_order_acquire) == failed);

   clock.advance(std::chrono::seconds{3});
   refresh.notify_verified_server();
   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > failed; }));

   refresh.request_stop();
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   running.get();
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_stop_cancels_and_joins_an_active_query) {
   namespace asio = boost::asio;

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(16);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(17)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto entered = std::make_shared<forge::asio::notification>();
   auto canceled = std::make_shared<forge::asio::notification>();
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::minutes{10},
           .query_timeout = std::chrono::minutes{1},
       }},
       [entered, canceled](protocol_id, dht::key, std::chrono::milliseconds,
                           std::shared_ptr<cancellation_latch> cancellation) -> asio::awaitable<bool> {
          const auto executor = co_await asio::this_coro::executor;
          auto wait = std::make_shared<asio::steady_timer>(executor);
          wait->expires_at(asio::steady_timer::time_point::max());
          auto subscription = cancellation_latch::subscribe(cancellation,
                                                             [wait]() noexcept { cancel_timer_noexcept(wait); });
          entered->notify();
          auto error = boost::system::error_code{};
          co_await wait->async_wait(asio::redirect_error(asio::use_awaitable, error));
          BOOST_TEST(error == asio::error::operation_aborted);
          canceled->notify();
          co_return false;
       },
       clock.source(),
   };

   const auto entered_epoch = entered->epoch();
   auto running = asio::co_spawn(runtime.context(), refresh.async_run(), asio::use_future);
   static_cast<void>(asio::co_spawn(runtime.context(), entered->async_wait(entered_epoch), asio::use_future).get());

   const auto canceled_epoch = canceled->epoch();
   refresh.request_stop();
   // The notification originates only after the child receives the manager-owned cancellation signal.
   static_cast<void>(asio::co_spawn(runtime.context(), canceled->async_wait(canceled_epoch), asio::use_future).get());
   running.get();
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_status_is_synchronized_with_wakeup_rescheduling) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto local = refresh_peer(21);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(22)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto queries = std::atomic_size_t{};
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::milliseconds{1},
           .query_timeout = std::chrono::milliseconds{250},
       }},
       [&queries](protocol_id, dht::key, std::chrono::milliseconds timeout,
                  std::shared_ptr<cancellation_latch>) -> boost::asio::awaitable<bool> {
          BOOST_TEST(timeout == std::chrono::milliseconds{250});
          queries.fetch_add(1, std::memory_order_acq_rel);
          co_return true;
       },
       clock.source(),
   };
   auto running = boost::asio::co_spawn(runtime.context(), refresh.async_run(), boost::asio::use_future);
   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > 1; }));

   auto missing_status = std::atomic_bool{};
   auto reader = std::jthread{[&] {
      for (auto attempt = 0U; attempt < 10'000U; ++attempt) {
         if (!refresh.status(builtins::kad_dht)) {
            missing_status.store(true, std::memory_order_release);
         }
      }
   }};
   for (auto attempt = 0U; attempt < 1'000U; ++attempt) {
      clock.advance(std::chrono::milliseconds{2});
      refresh.notify_verified_server();
      static_cast<void>(refresh.status(builtins::kad_dht));
   }
   reader.join();

   BOOST_TEST(!missing_status.load(std::memory_order_acquire));
   refresh.request_stop();
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   running.get();
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_saturates_maximum_interval_deadline) {
   namespace asio = boost::asio;

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(31);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(32)}, dht::routing_admission::verified_server);
   auto clock = fake_refresh_clock{};
   clock.set(detail::dht_routing_refresh::time_point::max() - detail::dht_routing_refresh::time_point::duration{1});
   auto observed_deadline = std::promise<detail::dht_routing_refresh::time_point>{};
   auto refresh_value = static_cast<detail::dht_routing_refresh*>(nullptr);
   auto captured = false;

   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = (std::chrono::milliseconds::max)(),
           .query_timeout = std::chrono::milliseconds{100},
       }},
       [](protocol_id, dht::key, std::chrono::milliseconds,
          std::shared_ptr<cancellation_latch>) -> asio::awaitable<bool> { co_return true; },
       detail::dht_routing_refresh::time_source{
           .now = [&clock] { return clock.now(); },
           .wait_until = [&observed_deadline, &refresh_value, &captured](
                             std::shared_ptr<detail::lifecycle_wakeup>, std::uint64_t,
                             detail::dht_routing_refresh::time_point deadline) -> asio::awaitable<std::uint64_t> {
              if (!captured) {
                 captured = true;
                 observed_deadline.set_value(deadline);
                 refresh_value->request_stop();
              }
              co_return 0;
           },
       },
   };
   refresh_value = std::addressof(refresh);

   auto running = asio::co_spawn(runtime.context(), refresh.async_run(), asio::use_future);
   const auto deadline = observed_deadline.get_future().get();
   BOOST_TEST(deadline.time_since_epoch().count() ==
              detail::dht_routing_refresh::time_point::max().time_since_epoch().count());
   BOOST_CHECK_NO_THROW(running.get());
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_does_not_run_saturated_deadline_at_clock_maximum) {
   namespace asio = boost::asio;

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(41);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(42)}, dht::routing_admission::verified_server);
   auto clock = fake_refresh_clock{};
   clock.set(detail::dht_routing_refresh::time_point::max() - detail::dht_routing_refresh::time_point::duration{1});
   auto queries = std::size_t{};
   auto queries_before_maximum = std::size_t{};
   auto repeated_at_maximum = false;
   auto waits = std::size_t{};
   auto refresh_value = static_cast<detail::dht_routing_refresh*>(nullptr);

   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = (std::chrono::milliseconds::max)(),
           .query_timeout = std::chrono::milliseconds{100},
       }},
       [&](protocol_id, dht::key, std::chrono::milliseconds,
           std::shared_ptr<cancellation_latch>) -> asio::awaitable<bool> {
          ++queries;
          if (clock.now() == detail::dht_routing_refresh::time_point::max() &&
              queries > queries_before_maximum) {
             repeated_at_maximum = true;
             refresh_value->request_stop();
          }
          co_return true;
       },
       detail::dht_routing_refresh::time_source{
           .now = [&clock] { return clock.now(); },
           .wait_until = [&](std::shared_ptr<detail::lifecycle_wakeup>, std::uint64_t observed,
                             detail::dht_routing_refresh::time_point deadline) -> asio::awaitable<std::uint64_t> {
              BOOST_TEST(deadline.time_since_epoch().count() ==
                         detail::dht_routing_refresh::time_point::max().time_since_epoch().count());
              ++waits;
              if (waits == 1U) {
                 queries_before_maximum = queries;
                 clock.set(detail::dht_routing_refresh::time_point::max());
              } else {
                 refresh_value->request_stop();
              }
              co_return observed + 1U;
           },
       },
   };
   refresh_value = std::addressof(refresh);

   auto running = asio::co_spawn(runtime.context(), refresh.async_run(), asio::use_future);
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_NO_THROW(running.get());
   BOOST_TEST(waits == 2U);
   BOOST_TEST(queries == queries_before_maximum);
   BOOST_TEST(!repeated_at_maximum);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
