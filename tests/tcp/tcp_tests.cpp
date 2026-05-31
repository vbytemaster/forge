#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.tcp.connector;
import fcl.tcp.exceptions;
import fcl.tcp.listener;
import fcl.tcp.options;
import fcl.tcp.transport;
import fcl.transport.endpoint;
import fcl.transport.exceptions;
import fcl.transport.frame;
import fcl.transport.registry;
import fcl.transport.stream;

namespace {

using bytes = std::vector<std::uint8_t>;

[[nodiscard]] bytes text_bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] fcl::transport::endpoint loopback(std::uint16_t port) {
   return fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::ip4,
                                   .protocol = fcl::transport::endpoint::protocol_kind::tcp,
                                   .host = "127.0.0.1",
                                   .port = port};
}

[[nodiscard]] fcl::transport::endpoint invalid_quic_endpoint() {
   return fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::ip4,
                                   .protocol = fcl::transport::endpoint::protocol_kind::quic_v1,
                                   .host = "127.0.0.1",
                                   .port = 1};
}

boost::asio::awaitable<void> tcp_roundtrip() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::tcp::listener{executor, loopback(0)};
   const auto local = listener.local_endpoint();
   BOOST_CHECK(local.protocol == fcl::transport::endpoint::protocol_kind::tcp);
   BOOST_CHECK_EQUAL(local.host, "127.0.0.1");
   BOOST_CHECK(local.port != 0);

   auto accept = boost::asio::co_spawn(executor, listener.async_accept(), boost::asio::use_awaitable);
   auto connector = fcl::tcp::connector{executor};
   auto client = co_await connector.async_connect(local);
   auto server = co_await std::move(accept);

   BOOST_CHECK(client.local_endpoint.protocol == fcl::transport::endpoint::protocol_kind::tcp);
   BOOST_CHECK_EQUAL(client.remote_endpoint.port, local.port);
   BOOST_CHECK_EQUAL(server.local_endpoint.port, local.port);
   BOOST_CHECK_EQUAL(server.remote_endpoint.port, client.local_endpoint.port);
   BOOST_CHECK(client.stream.valid());
   BOOST_CHECK(server.stream.valid());

   const auto ping = text_bytes("ping");
   co_await client.stream.async_write(ping);
   auto received_ping = co_await server.stream.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_ping.begin(), received_ping.end(), ping.begin(), ping.end());

   const auto pong = text_bytes("pong");
   co_await server.stream.async_write(pong);
   auto received_pong = co_await client.stream.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_pong.begin(), received_pong.end(), pong.begin(), pong.end());

   const auto framed = text_bytes("framed payload");
   co_await client.stream.async_write_frame(framed);
   auto received_frame = co_await server.stream.async_read_frame();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_frame.begin(), received_frame.end(), framed.begin(), framed.end());

   co_await client.stream.async_close();
   co_await server.stream.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> tcp_read_chunk_limit_is_behavioral() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::tcp::listener{executor, loopback(0), {}, fcl::tcp::options{.read_chunk_size = 4}};
   const auto local = listener.local_endpoint();

   auto accept = boost::asio::co_spawn(executor, listener.async_accept(), boost::asio::use_awaitable);
   auto connector = fcl::tcp::connector{executor, fcl::tcp::options{.read_chunk_size = 4}};
   auto client = co_await connector.async_connect(local);
   auto server = co_await std::move(accept);

   const auto outbound = text_bytes("abcdef");
   const auto expected = text_bytes("abcd");
   co_await client.stream.async_write(outbound);
   auto chunk = co_await server.stream.async_read();
   BOOST_CHECK_EQUAL(chunk.size(), 4U);
   BOOST_CHECK_EQUAL_COLLECTIONS(chunk.begin(), chunk.end(), expected.begin(), expected.end());

   co_await client.stream.async_close();
   co_await server.stream.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> tcp_registry_roundtrip() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto registry = fcl::transport::registry{};
   fcl::tcp::register_stream(registry, executor);

   auto listener = co_await registry.async_listen_stream(loopback(0));
   auto accept = boost::asio::co_spawn(executor, listener.async_accept(), boost::asio::use_awaitable);
   auto client = co_await registry.async_connect_stream(listener.local_endpoint());
   auto server = co_await std::move(accept);

   const auto payload = text_bytes("registry");
   co_await client.stream.async_write(payload);
   auto received = co_await server.stream.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received.begin(), received.end(), payload.begin(), payload.end());

   co_await client.stream.async_close();
   co_await server.stream.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> cancel_unblocks_accept() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::tcp::listener{executor, loopback(0)};
   auto timer = boost::asio::steady_timer{executor};
   timer.expires_after(std::chrono::milliseconds{25});
   boost::asio::co_spawn(
       executor,
       [&listener, timer = std::move(timer)]() mutable -> boost::asio::awaitable<void> {
          co_await timer.async_wait(boost::asio::use_awaitable);
          listener.cancel();
       },
       boost::asio::detached);

   try {
      (void)co_await listener.async_accept();
      BOOST_FAIL("accept should be canceled");
   } catch (const fcl::tcp::exceptions::canceled&) {
      co_return;
   }
}

boost::asio::awaitable<void> close_unblocks_accept() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::tcp::listener{executor, loopback(0)};
   auto timer = boost::asio::steady_timer{executor};
   timer.expires_after(std::chrono::milliseconds{25});
   boost::asio::co_spawn(
       executor,
       [&listener, timer = std::move(timer)]() mutable -> boost::asio::awaitable<void> {
          co_await timer.async_wait(boost::asio::use_awaitable);
          co_await listener.async_close();
       },
       boost::asio::detached);

   try {
      (void)co_await listener.async_accept();
      BOOST_FAIL("accept should be closed");
   } catch (const fcl::tcp::exceptions::closed&) {
      co_return;
   }
}

boost::asio::awaitable<void> tcp_invalid_endpoint_checks() {
   auto executor = co_await boost::asio::this_coro::executor;
   auto connector = fcl::tcp::connector{executor};
   BOOST_CHECK_THROW((void)co_await connector.async_connect(invalid_quic_endpoint()), fcl::tcp::exceptions::invalid_endpoint);
   BOOST_CHECK_THROW((void)co_await connector.async_connect(loopback(0)), fcl::tcp::exceptions::invalid_endpoint);

   auto dns_listen = fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::dns,
                                              .protocol = fcl::transport::endpoint::protocol_kind::tcp,
                                              .host = "localhost",
                                              .port = 0};
   BOOST_CHECK_THROW(((void)fcl::tcp::listener{executor, dns_listen}), fcl::tcp::exceptions::invalid_endpoint);

   auto refused_probe = fcl::tcp::listener{executor, loopback(0)};
   auto refused_endpoint = refused_probe.local_endpoint();
   co_await refused_probe.async_close();
   BOOST_CHECK_THROW((void)co_await connector.async_connect(refused_endpoint), fcl::tcp::exceptions::connect_failed);
}

} // namespace

BOOST_AUTO_TEST_SUITE(tcp)

BOOST_AUTO_TEST_CASE(tcp_stream_roundtrip_and_framing) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, tcp_roundtrip());
}

BOOST_AUTO_TEST_CASE(tcp_options_affect_stream_reads) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, tcp_read_chunk_limit_is_behavioral());
}

BOOST_AUTO_TEST_CASE(tcp_integrates_with_transport_registry) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, tcp_registry_roundtrip());
}

BOOST_AUTO_TEST_CASE(tcp_accept_can_be_canceled_or_closed) {
   auto runtime = fcl::asio::runtime{};
   BOOST_CHECK(fcl::asio::blocking::run_for(runtime, cancel_unblocks_accept(), std::chrono::seconds{2}));
   BOOST_CHECK(fcl::asio::blocking::run_for(runtime, close_unblocks_accept(), std::chrono::seconds{2}));
}

BOOST_AUTO_TEST_CASE(tcp_rejects_invalid_endpoints_and_refused_connects) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, tcp_invalid_endpoint_checks());
}

BOOST_AUTO_TEST_SUITE_END()
