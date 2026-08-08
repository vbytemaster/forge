module;

#include <boost/test/unit_test.hpp>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.blocking;
import forge.asio.runtime;
import forge.crypto.asymmetric;
import forge.net.transport.session;

#include "../../libraries/net/p2p/details/direct_transport.hxx"
#include "../../libraries/net/p2p/details/libp2p_identity_material.hxx"
#include "../../libraries/net/p2p/details/operation_deadline.hxx"
#include "../../libraries/net/p2p/details/session_teardown.hxx"

namespace forge::net::p2p {
namespace {

BOOST_AUTO_TEST_CASE(p2p_session_teardown_waits_for_started_transport_cleanup) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto release =
       std::make_shared<boost::asio::steady_timer>(runtime.context(), boost::asio::steady_timer::time_point::max());
   auto close_started = std::atomic_size_t{0};
   auto cancel_called = std::atomic_size_t{0};
   auto teardown = detail::session_teardown{runtime.context().get_executor()};

   auto operations = std::vector<detail::session_teardown::operation>{};
   for (auto remaining = 2U; remaining != 0U; --remaining) {
      operations.push_back(detail::session_teardown::operation{
          .close = [release, &close_started]() -> boost::asio::awaitable<void> {
             close_started.fetch_add(1, std::memory_order_release);
             auto error = boost::system::error_code{};
             co_await release->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
          },
          .cancel = [&cancel_called] { cancel_called.fetch_add(1, std::memory_order_release); },
      });
   }
   teardown.start(std::move(operations));

   const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (close_started.load(std::memory_order_acquire) != 2U) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < close_deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }

   auto cancellation = boost::asio::cancellation_signal{};
   auto stopped =
       boost::asio::co_spawn(runtime.context(), teardown.wait(),
                             boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   const auto waiting_for_cleanup = stopped.wait_for(std::chrono::milliseconds{50}) == std::future_status::timeout;
   BOOST_TEST(waiting_for_cleanup);

   cancellation.emit(boost::asio::cancellation_type::total);
   const auto cancellation_did_not_bypass_cleanup =
       stopped.wait_for(std::chrono::milliseconds{50}) == std::future_status::timeout;
   BOOST_TEST(cancellation_did_not_bypass_cleanup);

   boost::asio::post(runtime.context(), [release] { release->cancel(); });
   const auto cleanup_completed = stopped.wait_for(std::chrono::seconds{2}) == std::future_status::ready;
   BOOST_REQUIRE(cleanup_completed);
   stopped.get();
   BOOST_TEST(cancel_called.load(std::memory_order_acquire) == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_session_teardown_waits_for_tracked_background_operation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto teardown = detail::session_teardown{runtime.context().get_executor()};
   auto tracked = teardown.track();
   BOOST_REQUIRE(tracked.active());

   teardown.start({});
   auto stopped = boost::asio::co_spawn(runtime.context(), teardown.wait(), boost::asio::use_future);
   const auto waits_for_background_operation =
       stopped.wait_for(std::chrono::milliseconds{50}) == std::future_status::timeout;
   BOOST_TEST(waits_for_background_operation);

   tracked.release();
   BOOST_REQUIRE(stopped.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   stopped.get();
}

BOOST_AUTO_TEST_CASE(p2p_session_teardown_handles_concurrent_track_release_and_start) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   for (auto iteration = 0U; iteration < 1'000U; ++iteration) {
      auto teardown = detail::session_teardown{runtime.context().get_executor()};
      auto tracked = teardown.track();
      auto start = std::barrier{2};
      auto releaser = std::thread{[&] {
         start.arrive_and_wait();
         tracked.release();
      }};
      start.arrive_and_wait();
      teardown.start({});
      releaser.join();
      forge::asio::blocking::run(runtime, teardown.wait());
   }
}

BOOST_AUTO_TEST_CASE(p2p_operation_deadline_preserves_shutdown_winner_past_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto deadline = operation_deadline{runtime.context(), std::chrono::milliseconds{10}};
   auto canceled = std::atomic_bool{false};
   auto stopping = deadline.stopping();

   BOOST_REQUIRE(stopping.request_stop());
   deadline.arm([&] { canceled.store(true, std::memory_order_release); });
   std::this_thread::sleep_for(std::chrono::milliseconds{50});

   BOOST_TEST(deadline.stopped());
   BOOST_TEST(!deadline.timed_out());
   BOOST_TEST(!canceled.load(std::memory_order_acquire));
   BOOST_TEST(deadline.finish());
}

BOOST_AUTO_TEST_CASE(p2p_operation_deadline_preserves_timeout_winner_after_shutdown) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto deadline = operation_deadline{runtime.context(), std::chrono::milliseconds{10}};
   auto canceled = std::atomic_bool{false};
   auto stopping = deadline.stopping();
   deadline.arm([&] { canceled.store(true, std::memory_order_release); });

   const auto timeout_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (!canceled.load(std::memory_order_acquire)) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < timeout_deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }

   BOOST_TEST(deadline.timed_out());
   BOOST_TEST(!deadline.stopped());
   BOOST_TEST(!stopping.request_stop());
   BOOST_TEST(!deadline.finish());
}

BOOST_AUTO_TEST_CASE(p2p_direct_transport_teardown_continues_after_profile_failure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   const auto options = node::options{};
   const auto identity = make_libp2p_identity_material(options);
   auto registry = direct::registry{runtime, options, identity};
   auto failed_stop = std::atomic_size_t{0};
   auto next_stop = std::atomic_size_t{0};
   auto failed_async_stop = std::atomic_size_t{0};
   auto next_async_stop = std::atomic_size_t{0};

   const auto add_profile = [&](auto stop, auto async_stop) {
      registry.add(direct::profile{
          .supports = [](const endpoint&) { return false; },
          .listening = [] { return false; },
          .local_endpoints = [] { return std::vector<endpoint>{}; },
          .listen = [](endpoint value) { return value; },
          .stop = std::move(stop),
          .async_stop = std::move(async_stop),
          .async_connect = [](endpoint, const node::connect_options&) -> boost::asio::awaitable<direct::connection> {
             co_return direct::connection{};
          },
          .async_accept = [](endpoint) -> boost::asio::awaitable<direct::connection> {
             co_return direct::connection{};
          },
      });
   };

   add_profile(
       [&] {
          failed_stop.fetch_add(1, std::memory_order_release);
          throw std::runtime_error{"expected stop failure"};
       },
       [&]() -> boost::asio::awaitable<void> {
          failed_async_stop.fetch_add(1, std::memory_order_release);
          throw std::runtime_error{"expected async stop failure"};
          co_return;
       });
   add_profile([&] { next_stop.fetch_add(1, std::memory_order_release); },
               [&]() -> boost::asio::awaitable<void> {
                  next_async_stop.fetch_add(1, std::memory_order_release);
                  co_return;
               });

   auto teardown = detail::session_teardown{runtime.context().get_executor()};
   auto operations = std::vector<detail::session_teardown::operation>{};
   operations.push_back(registry.teardown_operation());
   registry.stop();
   teardown.start(std::move(operations));
   forge::asio::blocking::run(runtime, teardown.wait());

   BOOST_TEST(failed_stop.load(std::memory_order_acquire) == 1U);
   BOOST_TEST(next_stop.load(std::memory_order_acquire) == 1U);
   BOOST_TEST(failed_async_stop.load(std::memory_order_acquire) == 1U);
   BOOST_TEST(next_async_stop.load(std::memory_order_acquire) == 1U);
}

} // namespace
} // namespace forge::net::p2p
