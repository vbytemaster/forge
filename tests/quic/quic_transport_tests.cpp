#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.exceptions;
import fcl.quic.endpoint;
import fcl.quic.exceptions;
import fcl.quic.options;
import fcl.quic.security;
import fcl.quic.transport;
import fcl.transport.buffer;
import fcl.transport.connector;
import fcl.transport.endpoint;
import fcl.transport.exceptions;
import fcl.transport.listener;
import fcl.transport.registry;
import fcl.transport.session;
import fcl.transport.stream;

namespace {

using bytes = std::vector<std::uint8_t>;

template <typename T>
struct spawned_result {
   explicit spawned_result(boost::asio::any_io_executor executor)
       : timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   boost::asio::steady_timer timer;
   std::optional<T> value;
   std::exception_ptr error;
   bool done = false;
};

template <typename T>
[[nodiscard]] std::shared_ptr<spawned_result<T>> spawn_result(boost::asio::any_io_executor executor,
                                                             boost::asio::awaitable<T> operation) {
   auto state = std::make_shared<spawned_result<T>>(executor);
   boost::asio::co_spawn(
       executor,
       [state, operation = std::move(operation)]() mutable -> boost::asio::awaitable<void> {
          try {
             state->value.emplace(co_await std::move(operation));
          } catch (...) {
             state->error = std::current_exception();
          }
          state->done = true;
          state->timer.cancel();
       },
       boost::asio::detached);
   return state;
}

template <typename T>
boost::asio::awaitable<T> take_result(std::shared_ptr<spawned_result<T>> state) {
   auto error = boost::system::error_code{};
   while (!state->done) {
      co_await state->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error && error != boost::asio::error::operation_aborted) {
         throw boost::system::system_error{error};
      }
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   co_return std::move(*state->value);
}

struct pem_pair {
   std::string certificate;
   std::string private_key;
};

struct tls_material {
   pem_pair ca;
   pem_pair server;
};

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;
using x509_ptr = std::unique_ptr<X509, x509_deleter>;
using bio_ptr = std::unique_ptr<BIO, bio_deleter>;

[[nodiscard]] bytes text_bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] fcl::transport::endpoint loopback_quic(std::uint16_t port) {
   return fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::ip4,
                                   .protocol = fcl::transport::endpoint::protocol_kind::quic_v1,
                                   .host = "127.0.0.1",
                                   .port = port};
}

[[nodiscard]] fcl::transport::endpoint dns_quic(std::uint16_t port) {
   return fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::dns,
                                   .protocol = fcl::transport::endpoint::protocol_kind::quic_v1,
                                   .host = "localhost",
                                   .port = port};
}

[[nodiscard]] fcl::transport::endpoint tcp_endpoint(std::uint16_t port) {
   return fcl::transport::endpoint{.host_type = fcl::transport::endpoint::host_kind::ip4,
                                   .protocol = fcl::transport::endpoint::protocol_kind::tcp,
                                   .host = "127.0.0.1",
                                   .port = port};
}

[[nodiscard]] evp_pkey_ptr make_key() {
   auto context = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>{
       EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free};
   BOOST_REQUIRE(context != nullptr);
   BOOST_REQUIRE(EVP_PKEY_keygen_init(context.get()) == 1);
   BOOST_REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) == 1);
   auto* raw_key = static_cast<EVP_PKEY*>(nullptr);
   BOOST_REQUIRE(EVP_PKEY_keygen(context.get(), &raw_key) == 1);
   auto key = evp_pkey_ptr{raw_key};
   BOOST_REQUIRE(key != nullptr);
   return key;
}

void add_extension(X509* certificate, X509* issuer, int nid, std::string_view value) {
   auto context = X509V3_CTX{};
   X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
   auto* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, std::string{value}.c_str());
   BOOST_REQUIRE(extension != nullptr);
   X509_add_ext(certificate, extension, -1);
   X509_EXTENSION_free(extension);
}

[[nodiscard]] x509_ptr make_certificate(EVP_PKEY* subject_key, std::string_view common_name, std::uint32_t serial,
                                        X509* issuer, EVP_PKEY* issuer_key, bool ca,
                                        std::string_view san = {}) {
   auto certificate = x509_ptr{X509_new()};
   BOOST_REQUIRE(certificate != nullptr);
   X509_set_version(certificate.get(), 2);
   ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), static_cast<long>(serial));
   X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60);
   X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 24 * 60 * 60);
   X509_set_pubkey(certificate.get(), subject_key);

   auto* name = X509_get_subject_name(certificate.get());
   X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name.data()),
                              static_cast<int>(common_name.size()), -1, 0);
   if (issuer == nullptr) {
      X509_set_issuer_name(certificate.get(), name);
      issuer = certificate.get();
      issuer_key = subject_key;
   } else {
      X509_set_issuer_name(certificate.get(), X509_get_subject_name(issuer));
   }

   add_extension(certificate.get(), issuer, NID_basic_constraints, ca ? "critical,CA:TRUE" : "CA:FALSE");
   add_extension(certificate.get(), issuer, NID_key_usage,
                 ca ? "critical,keyCertSign,cRLSign" : "digitalSignature,keyEncipherment");
   if (!san.empty()) {
      add_extension(certificate.get(), issuer, NID_subject_alt_name, san);
   }

   BOOST_REQUIRE(X509_sign(certificate.get(), issuer_key, EVP_sha256()) > 0);
   return certificate;
}

[[nodiscard]] std::string write_certificate_pem(X509* certificate) {
   auto bio = bio_ptr{BIO_new(BIO_s_mem())};
   BOOST_REQUIRE(bio != nullptr);
   BOOST_REQUIRE(PEM_write_bio_X509(bio.get(), certificate) == 1);
   auto* data = static_cast<BUF_MEM*>(nullptr);
   BIO_get_mem_ptr(bio.get(), &data);
   return std::string{data->data, data->length};
}

[[nodiscard]] std::string write_private_key_pem(EVP_PKEY* key) {
   auto bio = bio_ptr{BIO_new(BIO_s_mem())};
   BOOST_REQUIRE(bio != nullptr);
   BOOST_REQUIRE(PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) == 1);
   auto* data = static_cast<BUF_MEM*>(nullptr);
   BIO_get_mem_ptr(bio.get(), &data);
   return std::string{data->data, data->length};
}

[[nodiscard]] bytes certificate_der_from_pem(std::string_view certificate_pem) {
   auto bio = bio_ptr{BIO_new_mem_buf(certificate_pem.data(), static_cast<int>(certificate_pem.size()))};
   BOOST_REQUIRE(bio != nullptr);
   auto certificate = x509_ptr{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
   BOOST_REQUIRE(certificate != nullptr);

   const auto len = i2d_X509(certificate.get(), nullptr);
   BOOST_REQUIRE(len > 0);
   auto out = bytes(static_cast<std::size_t>(len));
   auto* cursor = out.data();
   BOOST_REQUIRE(i2d_X509(certificate.get(), &cursor) == len);
   return out;
}

[[nodiscard]] tls_material make_tls_material() {
   auto ca_key = make_key();
   auto server_key = make_key();

   auto ca_certificate = make_certificate(ca_key.get(), "fcl quic test ca", 1, nullptr, nullptr, true);
   auto server_certificate = make_certificate(server_key.get(), "localhost", 2, ca_certificate.get(), ca_key.get(),
                                             false, "DNS:localhost,IP:127.0.0.1");

   return tls_material{.ca = {.certificate = write_certificate_pem(ca_certificate.get()),
                              .private_key = write_private_key_pem(ca_key.get())},
                       .server = {.certificate = write_certificate_pem(server_certificate.get()),
                                  .private_key = write_private_key_pem(server_key.get())}};
}

[[nodiscard]] fcl::quic::server_options make_server_options(const tls_material& material,
                                                            std::string alpn = "fcl-quic-transport/1",
                                                            fcl::quic::transport_limits limits = {}) {
   return fcl::quic::server_options{
       .alpn = std::move(alpn),
       .limits = limits,
       .security = fcl::quic::security_options{.verify_peer = false},
       .certificate_pem = material.server.certificate,
       .private_key_pem = material.server.private_key,
   };
}

[[nodiscard]] fcl::quic::client_options make_client_options(const tls_material& material,
                                                            std::string alpn = "fcl-quic-transport/1",
                                                            fcl::quic::transport_limits limits = {}) {
   auto out = fcl::quic::client_options{
       .alpn = std::move(alpn),
       .handshake_timeout = std::chrono::milliseconds{5'000},
       .limits = limits,
       .security = fcl::quic::security_options{.verify_peer = false},
   };
   (void)material;
   return out;
}

[[nodiscard]] fcl::quic::client_options make_pinned_client_options(const tls_material& material,
                                                                   std::string alpn = "fcl-quic-transport/1") {
   auto out = make_client_options(material, std::move(alpn));
   out.security.verify_peer = true;
   out.security.expected_sha256_fingerprint =
       fcl::quic::sha256_fingerprint(certificate_der_from_pem(material.server.certificate));
   return out;
}

[[nodiscard]] bool has_quic_code(const fcl::exceptions::base& error, fcl::quic::exceptions::code expected) {
   const auto actual = fcl::quic::exceptions::code_of(error);
   return actual && *actual == expected;
}

void require_quic_code(const fcl::exceptions::base& error, fcl::quic::exceptions::code expected) {
   BOOST_TEST_REQUIRE(has_quic_code(error, expected));
}

void require_transport_code(const fcl::exceptions::base& error, fcl::transport::exceptions::code expected) {
   BOOST_TEST_REQUIRE(fcl::transport::exceptions::is(error, expected));
}

boost::asio::awaitable<void> session_loopback_roundtrip(fcl::asio::runtime& runtime) {
   const auto material = make_tls_material();
   auto listener = fcl::quic::make_session_listener(runtime, loopback_quic(0), make_server_options(material));
   auto connector = fcl::quic::make_session_connector(runtime, make_client_options(material));
   auto executor = co_await boost::asio::this_coro::executor;

   auto accept = spawn_result<fcl::transport::session_connection>(executor, listener.async_accept());
   auto client = co_await connector.async_connect(listener.local_endpoint());
   auto server = co_await take_result(accept);

   BOOST_TEST(static_cast<int>(client.local_endpoint.protocol) ==
              static_cast<int>(fcl::transport::endpoint::protocol_kind::quic_v1));
   BOOST_TEST(static_cast<int>(client.remote_endpoint.protocol) ==
              static_cast<int>(fcl::transport::endpoint::protocol_kind::quic_v1));
   BOOST_TEST(client.local_endpoint.port != 0U);
   BOOST_TEST(client.remote_endpoint.port != 0U);
   BOOST_TEST(server.local_endpoint.port != 0U);
   BOOST_TEST(server.remote_endpoint.port != 0U);

   auto inbound = spawn_result<fcl::transport::stream>(executor, server.session.async_accept_stream());
   auto outbound = co_await client.session.async_open_stream();

   const auto payload = text_bytes("quic transport session");
   co_await outbound.async_write_frame(payload);
   auto accepted = co_await take_result(inbound);
   auto received = co_await accepted.async_read_frame();
   BOOST_TEST(received == payload, boost::test_tools::per_element());

   const auto chunk_payload = text_bytes("quic chunk");
   co_await outbound.async_write(fcl::transport::chunk{chunk_payload});
   auto received_chunk = co_await accepted.async_read_chunk();
   BOOST_TEST(received_chunk.to_vector() == chunk_payload, boost::test_tools::per_element());

   const auto reply = text_bytes("session reply");
   co_await accepted.async_write_frame(reply);
   auto echoed = co_await outbound.async_read_frame();
   BOOST_TEST(echoed == reply, boost::test_tools::per_element());

   const auto frame_chunk = text_bytes("session chunk reply");
   co_await accepted.async_write_frame(fcl::transport::chunk{frame_chunk});
   auto echoed_chunk = co_await outbound.async_read_frame_chunk();
   BOOST_TEST(echoed_chunk.to_vector() == frame_chunk, boost::test_tools::per_element());

   co_await outbound.async_close();
   co_await accepted.async_close();
   co_await client.session.async_close();
   co_await server.session.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> registry_roundtrip(fcl::asio::runtime& runtime) {
   const auto material = make_tls_material();
   auto registry = fcl::transport::registry{};
   fcl::quic::register_session(registry, runtime, make_client_options(material), make_server_options(material));
   BOOST_TEST(registry.has_session(fcl::transport::endpoint::protocol_kind::quic_v1));

   auto listener = co_await registry.async_listen_session(loopback_quic(0));
   auto executor = co_await boost::asio::this_coro::executor;
   auto accept = spawn_result<fcl::transport::session_connection>(executor, listener.async_accept());
   auto client = co_await registry.async_connect_session(listener.local_endpoint());
   auto server = co_await take_result(accept);

   auto inbound = spawn_result<fcl::transport::stream>(executor, server.session.async_accept_stream());
   auto outbound = co_await client.session.async_open_stream();
   const auto payload = text_bytes("registry path");
   co_await outbound.async_write(payload);
   auto accepted = co_await take_result(inbound);
   auto received = co_await accepted.async_read();
   BOOST_TEST(received == payload, boost::test_tools::per_element());

   co_await outbound.async_close();
   co_await accepted.async_close();
   co_await client.session.async_close();
   co_await server.session.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> options_and_limit_override(fcl::asio::runtime& runtime) {
   const auto material = make_tls_material();
   const auto alpn = std::string{"fcl-quic-transport-custom/2"};
   auto listener = fcl::quic::make_session_listener(runtime, loopback_quic(0), make_server_options(material, alpn));
   auto connector = fcl::quic::make_session_connector(runtime, make_pinned_client_options(material, alpn));
   auto executor = co_await boost::asio::this_coro::executor;

   auto accept = spawn_result<fcl::transport::session_connection>(executor, listener.async_accept());
   auto client = co_await connector.async_connect(
       listener.local_endpoint(),
       fcl::transport::connect_options{
           .limits = fcl::transport::limits{.max_streams_per_connection = 1},
       });
   auto server = co_await take_result(accept);

   auto first = co_await client.session.async_open_stream();
   BOOST_TEST(first.valid());
   try {
      (void)co_await client.session.async_open_stream();
      BOOST_FAIL("expected stream limit rejection from transport override");
   } catch (const fcl::exceptions::base& error) {
      require_quic_code(error, fcl::quic::exceptions::code::backpressure_rejected);
   }

   co_await first.async_close();
   co_await client.session.async_close();
   co_await server.session.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> invalid_endpoints_are_typed(fcl::asio::runtime& runtime) {
   const auto material = make_tls_material();
   auto connector = fcl::quic::make_session_connector(runtime, make_client_options(material));

   try {
      (void)co_await connector.async_connect(tcp_endpoint(9443));
      BOOST_FAIL("expected invalid protocol rejection");
   } catch (const fcl::exceptions::base& error) {
      require_quic_code(error, fcl::quic::exceptions::code::invalid_endpoint);
   }

   try {
      (void)co_await connector.async_connect(loopback_quic(0));
      BOOST_FAIL("expected zero-port rejection");
   } catch (const fcl::exceptions::base& error) {
      require_quic_code(error, fcl::quic::exceptions::code::invalid_endpoint);
   }

   try {
      auto listener = fcl::quic::make_session_listener(runtime, dns_quic(0), make_server_options(material));
      (void)listener;
      BOOST_FAIL("expected DNS listen rejection");
   } catch (const fcl::exceptions::base& error) {
      require_quic_code(error, fcl::quic::exceptions::code::invalid_endpoint);
   }
}

boost::asio::awaitable<void> cancellation_unblocks_listener_and_rejects_connector(fcl::asio::runtime& runtime) {
   const auto material = make_tls_material();
   auto listener = fcl::quic::make_session_listener(runtime, loopback_quic(0), make_server_options(material));
   const auto local = listener.local_endpoint();
   auto executor = co_await boost::asio::this_coro::executor;
   auto accept = spawn_result<fcl::transport::session_connection>(executor, listener.async_accept());
   listener.cancel();

   try {
      (void)co_await take_result(accept);
      BOOST_FAIL("expected canceled listener accept to fail");
   } catch (const fcl::exceptions::base& error) {
      const auto acceptable = has_quic_code(error, fcl::quic::exceptions::code::connection_closed) ||
                              has_quic_code(error, fcl::quic::exceptions::code::canceled);
      BOOST_TEST(acceptable);
   }

   auto connector = fcl::quic::make_session_connector(runtime, make_client_options(material));
   BOOST_TEST(connector.valid());
   connector.cancel();
   BOOST_TEST(!connector.valid());
   try {
      (void)co_await connector.async_connect(local);
      BOOST_FAIL("expected canceled connector to reject new connects");
   } catch (const fcl::exceptions::base& error) {
      require_transport_code(error, fcl::transport::exceptions::code::closed);
   }
}

} // namespace

BOOST_AUTO_TEST_SUITE(fcl_quic_transport_tests)

BOOST_AUTO_TEST_CASE(endpoint_conversion_preserves_transport_shape) {
   const auto ipv4 =
       fcl::quic::to_transport_endpoint(fcl::quic::endpoint{.host = "127.0.0.1", .port = 9443});
   BOOST_TEST(static_cast<int>(ipv4.host_type) == static_cast<int>(fcl::transport::endpoint::host_kind::ip4));
   BOOST_TEST(static_cast<int>(ipv4.protocol) ==
              static_cast<int>(fcl::transport::endpoint::protocol_kind::quic_v1));
   BOOST_TEST(ipv4.host == "127.0.0.1");
   BOOST_TEST(ipv4.port == 9443U);

   const auto ipv6 = fcl::quic::to_transport_endpoint(fcl::quic::endpoint{.host = "::1", .port = 9444});
   BOOST_TEST(static_cast<int>(ipv6.host_type) == static_cast<int>(fcl::transport::endpoint::host_kind::ip6));
   BOOST_TEST(static_cast<int>(ipv6.protocol) ==
              static_cast<int>(fcl::transport::endpoint::protocol_kind::quic_v1));

   const auto dns = fcl::quic::to_transport_endpoint(fcl::quic::endpoint{.host = "localhost", .port = 9445});
   BOOST_TEST(static_cast<int>(dns.host_type) == static_cast<int>(fcl::transport::endpoint::host_kind::dns));
   BOOST_TEST(static_cast<int>(dns.protocol) ==
              static_cast<int>(fcl::transport::endpoint::protocol_kind::quic_v1));

   const auto roundtrip = fcl::quic::from_transport_endpoint(ipv4);
   BOOST_TEST(roundtrip.host == "127.0.0.1");
   BOOST_TEST(roundtrip.port == 9443U);

   BOOST_CHECK_EXCEPTION(
       (void)fcl::quic::from_transport_endpoint(tcp_endpoint(9443)), fcl::exceptions::base,
       [](const fcl::exceptions::base& error) {
          return has_quic_code(error, fcl::quic::exceptions::code::invalid_endpoint);
       });
}

BOOST_AUTO_TEST_CASE(loopback_session_connector_listener_transfer_frames) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   fcl::asio::blocking::run(runtime, session_loopback_roundtrip(runtime));
}

BOOST_AUTO_TEST_CASE(registry_connect_listen_routes_quic_sessions) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   fcl::asio::blocking::run(runtime, registry_roundtrip(runtime));
}

BOOST_AUTO_TEST_CASE(custom_options_and_transport_limits_reach_runtime) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   fcl::asio::blocking::run(runtime, options_and_limit_override(runtime));
}

BOOST_AUTO_TEST_CASE(invalid_transport_endpoints_throw_typed_quic_errors) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   fcl::asio::blocking::run(runtime, invalid_endpoints_are_typed(runtime));
}

BOOST_AUTO_TEST_CASE(cancel_contract_rejects_and_unblocks) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   fcl::asio::blocking::run(runtime, cancellation_unblocks_listener_and_rejects_connector(runtime));
}

BOOST_AUTO_TEST_SUITE_END()
