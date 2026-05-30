#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.transport.connector;
import fcl.transport.endpoint;
import fcl.transport.exceptions;
import fcl.transport.frame;
import fcl.transport.listener;
import fcl.transport.registry;
import fcl.transport.session;
import fcl.transport.stream;

namespace {

using bytes = std::vector<std::uint8_t>;

[[nodiscard]] bytes text_bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] fcl::transport::endpoint endpoint(std::string host,
                                                fcl::transport::endpoint::protocol_kind protocol,
                                                std::uint16_t port) {
   return fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::ip4,
                                   .protocol = protocol,
                                   .host = std::move(host),
                                   .port = port};
}

class fake_stream final : public fcl::transport::detail::stream_concept {
 public:
   explicit fake_stream(std::int64_t stream_id) : id_{stream_id} {}

   [[nodiscard]] bool valid() const noexcept override {
      return open;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> value) override {
      writes.push_back({value.begin(), value.end()});
      co_return;
   }

   boost::asio::awaitable<bytes> async_read() override {
      ++reads_started;
      BOOST_REQUIRE(!reads.empty());
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      open = false;
      ++close_count;
      co_return;
   }

   std::deque<bytes> reads;
   std::vector<bytes> writes;
   std::uint64_t reads_started = 0;
   std::uint64_t close_count = 0;
   bool open = true;

 private:
   std::int64_t id_ = 0;
};

class fake_session final : public fcl::transport::detail::session_concept {
 public:
   [[nodiscard]] bool valid() const noexcept override {
      return open;
   }

   boost::asio::awaitable<fcl::transport::stream> async_open_stream() override {
      ++open_stream_count;
      co_return fcl::transport::detail::stream_access::make(std::make_shared<fake_stream>(100 + open_stream_count));
   }

   boost::asio::awaitable<fcl::transport::stream> async_accept_stream() override {
      ++accept_stream_count;
      BOOST_REQUIRE(!accepted.empty());
      auto out = std::move(accepted.front());
      accepted.pop_front();
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      open = false;
      ++close_count;
      co_return;
   }

   void cancel() override {
      open = false;
      ++cancel_count;
   }

   std::deque<fcl::transport::stream> accepted;
   std::uint64_t open_stream_count = 0;
   std::uint64_t accept_stream_count = 0;
   std::uint64_t close_count = 0;
   std::uint64_t cancel_count = 0;
   bool open = true;
};

class fake_stream_connector final : public fcl::transport::detail::stream_connector_concept {
 public:
   explicit fake_stream_connector(fcl::transport::endpoint local_value) : local{std::move(local_value)} {}

   [[nodiscard]] bool valid() const noexcept override {
      return active;
   }

   boost::asio::awaitable<fcl::transport::stream_connection>
   async_connect(fcl::transport::endpoint remote, fcl::transport::connect_options options) override {
      ++connect_count;
      last_remote = remote;
      last_limits = options.limits;
      co_return fcl::transport::stream_connection{
          .local_endpoint = local,
          .remote_endpoint = std::move(remote),
          .stream = fcl::transport::detail::stream_access::make(std::make_shared<fake_stream>(700)),
      };
   }

   void cancel() override {
      active = false;
      ++cancel_count;
   }

   fcl::transport::endpoint local;
   fcl::transport::endpoint last_remote;
   fcl::transport::limits last_limits;
   std::uint64_t connect_count = 0;
   std::uint64_t cancel_count = 0;
   bool active = true;
};

class fake_session_connector final : public fcl::transport::detail::session_connector_concept {
 public:
   explicit fake_session_connector(fcl::transport::endpoint local_value) : local{std::move(local_value)} {}

   [[nodiscard]] bool valid() const noexcept override {
      return active;
   }

   boost::asio::awaitable<fcl::transport::session_connection>
   async_connect(fcl::transport::endpoint remote, fcl::transport::connect_options options) override {
      ++connect_count;
      last_remote = remote;
      last_limits = options.limits;
      co_return fcl::transport::session_connection{
          .local_endpoint = local,
          .remote_endpoint = std::move(remote),
          .session = fcl::transport::detail::session_access::make(std::make_shared<fake_session>()),
      };
   }

   void cancel() override {
      active = false;
      ++cancel_count;
   }

   fcl::transport::endpoint local;
   fcl::transport::endpoint last_remote;
   fcl::transport::limits last_limits;
   std::uint64_t connect_count = 0;
   std::uint64_t cancel_count = 0;
   bool active = true;
};

class fake_stream_listener final : public fcl::transport::detail::stream_listener_concept {
 public:
   explicit fake_stream_listener(fcl::transport::endpoint local_value) : local{std::move(local_value)} {}

   [[nodiscard]] bool valid() const noexcept override {
      return active;
   }

   [[nodiscard]] fcl::transport::endpoint local_endpoint() const override {
      return local;
   }

   boost::asio::awaitable<fcl::transport::stream_connection> async_accept() override {
      ++accept_count;
      BOOST_REQUIRE(!accepted.empty());
      auto out = std::move(accepted.front());
      accepted.pop_front();
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      active = false;
      ++close_count;
      co_return;
   }

   void cancel() override {
      active = false;
      ++cancel_count;
   }

   fcl::transport::endpoint local;
   std::deque<fcl::transport::stream_connection> accepted;
   std::uint64_t accept_count = 0;
   std::uint64_t close_count = 0;
   std::uint64_t cancel_count = 0;
   bool active = true;
};

class fake_session_listener final : public fcl::transport::detail::session_listener_concept {
 public:
   explicit fake_session_listener(fcl::transport::endpoint local_value) : local{std::move(local_value)} {}

   [[nodiscard]] bool valid() const noexcept override {
      return active;
   }

   [[nodiscard]] fcl::transport::endpoint local_endpoint() const override {
      return local;
   }

   boost::asio::awaitable<fcl::transport::session_connection> async_accept() override {
      ++accept_count;
      BOOST_REQUIRE(!accepted.empty());
      auto out = std::move(accepted.front());
      accepted.pop_front();
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      active = false;
      ++close_count;
      co_return;
   }

   void cancel() override {
      active = false;
      ++cancel_count;
   }

   fcl::transport::endpoint local;
   std::deque<fcl::transport::session_connection> accepted;
   std::uint64_t accept_count = 0;
   std::uint64_t close_count = 0;
   std::uint64_t cancel_count = 0;
   bool active = true;
};

[[nodiscard]] fcl::transport::stream make_stream(std::shared_ptr<fake_stream> model) {
   return fcl::transport::detail::stream_access::make(std::move(model));
}

[[nodiscard]] fcl::transport::session make_session(std::shared_ptr<fake_session> model) {
   return fcl::transport::detail::session_access::make(std::move(model));
}

[[nodiscard]] fcl::transport::stream_connector make_stream_connector(std::shared_ptr<fake_stream_connector> model) {
   return fcl::transport::detail::stream_connector_access::make(std::move(model));
}

[[nodiscard]] fcl::transport::session_connector make_session_connector(std::shared_ptr<fake_session_connector> model) {
   return fcl::transport::detail::session_connector_access::make(std::move(model));
}

[[nodiscard]] fcl::transport::stream_listener make_stream_listener(std::shared_ptr<fake_stream_listener> model) {
   return fcl::transport::detail::stream_listener_access::make(std::move(model));
}

[[nodiscard]] fcl::transport::session_listener make_session_listener(std::shared_ptr<fake_session_listener> model) {
   return fcl::transport::detail::session_listener_access::make(std::move(model));
}

} // namespace

BOOST_AUTO_TEST_SUITE(transport)

BOOST_AUTO_TEST_CASE(transport_stream_delegates_and_preserves_buffered_frames) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_stream>(42);
   auto combined = fcl::transport::encode_frame(text_bytes("one"));
   auto second = fcl::transport::encode_frame(text_bytes("two"));
   combined.insert(combined.end(), second.begin(), second.end());
   model->reads.push_back(combined);

   auto value = make_stream(model);
   BOOST_CHECK(value.valid());
   BOOST_CHECK_EQUAL(value.id(), 42);

   const auto expected_one = text_bytes("one");
   const auto expected_two = text_bytes("two");
   auto first_payload = fcl::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_CHECK_EQUAL_COLLECTIONS(first_payload.begin(), first_payload.end(), expected_one.begin(), expected_one.end());
   auto second_payload = fcl::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_CHECK_EQUAL_COLLECTIONS(second_payload.begin(), second_payload.end(), expected_two.begin(), expected_two.end());
   BOOST_CHECK_EQUAL(model->reads_started, 1U);

   fcl::asio::blocking::run(runtime, value.async_write_frame(text_bytes("out")));
   const auto expected_write = fcl::transport::encode_frame(text_bytes("out"));
   BOOST_REQUIRE_EQUAL(model->writes.size(), 1U);
   BOOST_CHECK_EQUAL_COLLECTIONS(
       model->writes.front().begin(), model->writes.front().end(), expected_write.begin(), expected_write.end());

   fcl::asio::blocking::run(runtime, value.async_close());
   BOOST_CHECK_EQUAL(model->close_count, 1U);
   BOOST_CHECK(!value.valid());
}

BOOST_AUTO_TEST_CASE(transport_session_delegates_open_accept_close_cancel) {
   auto runtime = fcl::asio::runtime{};
   auto model = std::make_shared<fake_session>();
   model->accepted.push_back(make_stream(std::make_shared<fake_stream>(501)));

   auto value = make_session(model);
   BOOST_CHECK(value.valid());

   auto opened = fcl::asio::blocking::run(runtime, value.async_open_stream());
   BOOST_CHECK_EQUAL(opened.id(), 101);
   auto accepted = fcl::asio::blocking::run(runtime, value.async_accept_stream());
   BOOST_CHECK_EQUAL(accepted.id(), 501);
   BOOST_CHECK_EQUAL(model->open_stream_count, 1U);
   BOOST_CHECK_EQUAL(model->accept_stream_count, 1U);

   value.cancel();
   BOOST_CHECK_EQUAL(model->cancel_count, 1U);
   BOOST_CHECK(!value.valid());
}

BOOST_AUTO_TEST_CASE(transport_frame_handles_partial_multiple_and_limit) {
   const auto encoded_a = fcl::transport::encode_frame(text_bytes("a"));
   auto partial = bytes{encoded_a.begin(), encoded_a.begin() + 2};
   auto decoded_partial = fcl::transport::decode_frame(partial);
   BOOST_CHECK(decoded_partial.status == fcl::transport::frame_decode_status::need_more_data);

   auto encoded_b = fcl::transport::encode_frame(text_bytes("b"));
   auto combined = encoded_a;
   combined.insert(combined.end(), encoded_b.begin(), encoded_b.end());
   auto first = fcl::transport::decode_frame(combined);
   BOOST_REQUIRE(first.status == fcl::transport::frame_decode_status::complete);
   BOOST_CHECK_EQUAL(first.consumed, encoded_a.size());
   const auto expected_a = text_bytes("a");
   const auto expected_b = text_bytes("b");
   BOOST_CHECK_EQUAL_COLLECTIONS(first.payload.begin(), first.payload.end(), expected_a.begin(), expected_a.end());
   auto second = fcl::transport::decode_frame({combined.data() + first.consumed, combined.size() - first.consumed});
   BOOST_REQUIRE(second.status == fcl::transport::frame_decode_status::complete);
   BOOST_CHECK_EQUAL_COLLECTIONS(second.payload.begin(), second.payload.end(), expected_b.begin(), expected_b.end());

   BOOST_CHECK_THROW((void)fcl::transport::encode_frame(text_bytes("toolong"), fcl::transport::frame_options{.max_size = 3}),
                     fcl::transport::exceptions::frame_too_large);
   const auto over_limit = fcl::transport::encode_frame(text_bytes("toolong"));
   BOOST_CHECK_THROW((void)fcl::transport::decode_frame(over_limit, fcl::transport::frame_options{.max_size = 3}),
                     fcl::transport::exceptions::frame_too_large);
}

BOOST_AUTO_TEST_CASE(transport_endpoint_formats_authority_by_host_kind) {
   auto ip4 = endpoint("127.0.0.1", fcl::transport::endpoint::protocol_kind::tcp, 443);
   BOOST_CHECK_EQUAL(ip4.authority(), "127.0.0.1:443");

   auto dns = endpoint("example.com", fcl::transport::endpoint::protocol_kind::tcp, 8443);
   dns.host_type = fcl::transport::endpoint::host_kind::dns;
   BOOST_CHECK_EQUAL(dns.authority(), "example.com:8443");

   auto ip6 = endpoint("2001:db8::1", fcl::transport::endpoint::protocol_kind::tcp, 443);
   ip6.host_type = fcl::transport::endpoint::host_kind::ip6;
   BOOST_CHECK_EQUAL(ip6.authority(), "[2001:db8::1]:443");
}

BOOST_AUTO_TEST_CASE(transport_connector_listener_wrappers_preserve_endpoints) {
   auto runtime = fcl::asio::runtime{};
   const auto local = endpoint("127.0.0.1", fcl::transport::endpoint::protocol_kind::tcp, 7001);
   const auto remote = endpoint("127.0.0.2", fcl::transport::endpoint::protocol_kind::tcp, 7002);
   const auto connection_limits = fcl::transport::limits{.max_connections = 7};

   auto stream_connector_model = std::make_shared<fake_stream_connector>(local);
   auto stream_connector = make_stream_connector(stream_connector_model);
   auto stream_conn = fcl::asio::blocking::run(
       runtime, stream_connector.async_connect(remote, fcl::transport::connect_options{.limits = connection_limits}));
   BOOST_CHECK_EQUAL(stream_conn.local_endpoint.port, local.port);
   BOOST_CHECK_EQUAL(stream_conn.remote_endpoint.host, remote.host);
   BOOST_CHECK_EQUAL(stream_conn.stream.id(), 700);
   BOOST_CHECK_EQUAL(stream_connector_model->last_limits.max_connections, connection_limits.max_connections);
   stream_connector.cancel();
   BOOST_CHECK_EQUAL(stream_connector_model->cancel_count, 1U);
   BOOST_CHECK(!stream_connector.valid());

   auto session_connector_model = std::make_shared<fake_session_connector>(local);
   auto session_connector = make_session_connector(session_connector_model);
   auto session_conn = fcl::asio::blocking::run(
       runtime, session_connector.async_connect(remote, fcl::transport::connect_options{.limits = connection_limits}));
   BOOST_CHECK_EQUAL(session_conn.local_endpoint.port, local.port);
   BOOST_CHECK_EQUAL(session_conn.remote_endpoint.host, remote.host);
   BOOST_CHECK(session_conn.session.valid());
   BOOST_CHECK_EQUAL(session_connector_model->last_limits.max_connections, connection_limits.max_connections);

   auto stream_listener_model = std::make_shared<fake_stream_listener>(local);
   stream_listener_model->accepted.push_back(fcl::transport::stream_connection{
       .local_endpoint = local,
       .remote_endpoint = remote,
       .stream = make_stream(std::make_shared<fake_stream>(800)),
   });
   auto stream_listener = make_stream_listener(stream_listener_model);
   BOOST_CHECK_EQUAL(stream_listener.local_endpoint().port, local.port);
   auto accepted_stream = fcl::asio::blocking::run(runtime, stream_listener.async_accept());
   BOOST_CHECK_EQUAL(accepted_stream.stream.id(), 800);
   fcl::asio::blocking::run(runtime, stream_listener.async_close());
   BOOST_CHECK(!stream_listener.valid());

   auto session_listener_model = std::make_shared<fake_session_listener>(local);
   session_listener_model->accepted.push_back(fcl::transport::session_connection{
       .local_endpoint = local,
       .remote_endpoint = remote,
       .session = make_session(std::make_shared<fake_session>()),
   });
   auto session_listener = make_session_listener(session_listener_model);
   auto accepted_session = fcl::asio::blocking::run(runtime, session_listener.async_accept());
   BOOST_CHECK(accepted_session.session.valid());
   session_listener.cancel();
   BOOST_CHECK_EQUAL(session_listener_model->cancel_count, 1U);
}

BOOST_AUTO_TEST_CASE(transport_registry_routes_and_rejects_missing_or_duplicate) {
   auto runtime = fcl::asio::runtime{};
   auto registry = fcl::transport::registry{};
   const auto local = endpoint("127.0.0.1", fcl::transport::endpoint::protocol_kind::tcp, 9001);
   const auto remote = endpoint("127.0.0.2", fcl::transport::endpoint::protocol_kind::tcp, 9002);

   auto connector_model = std::make_shared<fake_stream_connector>(local);
   auto listener_model = std::make_shared<fake_stream_listener>(local);
   auto received_listen_limits = fcl::transport::limits{};
   registry.register_stream(
       fcl::transport::endpoint::protocol_kind::tcp,
       [connector_model] { return make_stream_connector(connector_model); },
       [listener_model, &received_listen_limits](fcl::transport::endpoint, fcl::transport::listen_options options)
           -> boost::asio::awaitable<fcl::transport::stream_listener> {
          received_listen_limits = options.limits;
          co_return make_stream_listener(listener_model);
       });
   BOOST_CHECK(registry.has_stream(fcl::transport::endpoint::protocol_kind::tcp));
   BOOST_CHECK(!registry.has_session(fcl::transport::endpoint::protocol_kind::tcp));

   auto connected = fcl::asio::blocking::run(
       runtime, registry.async_connect_stream(remote, fcl::transport::connect_options{.limits = {.max_connections = 11}}));
   BOOST_CHECK_EQUAL(connected.local_endpoint.port, local.port);
   BOOST_CHECK_EQUAL(connected.remote_endpoint.port, remote.port);
   BOOST_CHECK_EQUAL(connector_model->connect_count, 1U);
   BOOST_CHECK_EQUAL(connector_model->last_limits.max_connections, 11U);

   auto listener = fcl::asio::blocking::run(
       runtime, registry.async_listen_stream(local, fcl::transport::listen_options{.limits = {.max_connections = 13}}));
   BOOST_CHECK_EQUAL(listener.local_endpoint().port, local.port);
   BOOST_CHECK_EQUAL(received_listen_limits.max_connections, 13U);

   BOOST_CHECK_THROW(registry.register_stream(
                         fcl::transport::endpoint::protocol_kind::tcp,
                         [] { return fcl::transport::stream_connector{}; },
                         [](fcl::transport::endpoint, fcl::transport::listen_options)
                             -> boost::asio::awaitable<fcl::transport::stream_listener> {
                            co_return fcl::transport::stream_listener{};
                         }),
                     fcl::transport::exceptions::duplicate_registration);

   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, registry.async_connect_session(remote, {})),
                     fcl::transport::exceptions::unsupported_protocol);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, registry.async_listen_session(remote, {})),
                     fcl::transport::exceptions::unsupported_protocol);
}

BOOST_AUTO_TEST_SUITE_END()
