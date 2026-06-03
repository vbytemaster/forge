#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.crypto.x509;
import fcl.stcp.connection;
import fcl.stcp.connector;
import fcl.stcp.exceptions;
import fcl.stcp.listener;
import fcl.stcp.options;
import fcl.tcp.connector;
import fcl.tcp.listener;
import fcl.transport.endpoint;
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
   pem_pair client;
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

[[nodiscard]] fcl::transport::endpoint loopback(std::uint16_t port) {
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

[[nodiscard]] tls_material make_tls_material() {
   auto ca_key = make_key();
   auto server_key = make_key();
   auto client_key = make_key();

   auto ca_certificate = make_certificate(ca_key.get(), "fcl test ca", 1, nullptr, nullptr, true);
   auto server_certificate = make_certificate(server_key.get(), "localhost", 2, ca_certificate.get(), ca_key.get(), false,
                                             "DNS:localhost,IP:127.0.0.1");
   auto client_certificate =
       make_certificate(client_key.get(), "fcl test client", 3, ca_certificate.get(), ca_key.get(), false);

   return tls_material{.ca = {.certificate = write_certificate_pem(ca_certificate.get()),
                              .private_key = write_private_key_pem(ca_key.get())},
                       .server = {.certificate = write_certificate_pem(server_certificate.get()),
                                  .private_key = write_private_key_pem(server_key.get())},
                       .client = {.certificate = write_certificate_pem(client_certificate.get()),
                                  .private_key = write_private_key_pem(client_key.get())}};
}

[[nodiscard]] fcl::stcp::server_options server_options(const tls_material& material, bool verify_peer = false) {
   auto out = fcl::stcp::server_options{};
   out.certificate_pem = material.server.certificate;
   out.private_key_pem = material.server.private_key;
   out.security.verify_peer = verify_peer;
   out.security.trusted_ca_pem = material.ca.certificate;
   out.alpn_protocols = {"fcl-test/1"};
   return out;
}

[[nodiscard]] fcl::stcp::client_options client_options(const tls_material& material, bool with_certificate = false) {
   auto out = fcl::stcp::client_options{};
   out.security.trusted_ca_pem = material.ca.certificate;
   out.server_name = "localhost";
   out.sni = fcl::stcp::sni_policy::explicit_name;
   out.alpn_protocols = {"fcl-test/1"};
   if (with_certificate) {
      out.certificate_pem = material.client.certificate;
      out.private_key_pem = material.client.private_key;
   }
   return out;
}

struct raw_tls_observation {
   std::string sni;
};

int capture_sni_callback(SSL* ssl, int*, void* arg) {
   auto* observation = static_cast<raw_tls_observation*>(arg);
   if (const auto* name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name)) {
      observation->sni = name;
   }
   return SSL_TLSEXT_ERR_OK;
}

boost::asio::awaitable<raw_tls_observation> accept_raw_tls_once(fcl::tcp::listener& listener,
                                                               const tls_material& material) {
   namespace asio = boost::asio;
   auto observation = raw_tls_observation{};
   auto tcp = co_await listener.async_accept_connection();
   auto context = asio::ssl::context{asio::ssl::context::tls_server};
   context.use_certificate_chain(asio::buffer(material.server.certificate.data(), material.server.certificate.size()));
   context.use_private_key(asio::buffer(material.server.private_key.data(), material.server.private_key.size()),
                           asio::ssl::context::pem);
   SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_3_VERSION);
   SSL_CTX_set_tlsext_servername_callback(context.native_handle(), capture_sni_callback);
   SSL_CTX_set_tlsext_servername_arg(context.native_handle(), &observation);
   auto stream = asio::ssl::stream<boost::asio::ip::tcp::socket>{std::move(tcp).release_socket(), context};
   auto error = boost::system::error_code{};
   co_await stream.async_handshake(asio::ssl::stream_base::server,
                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
   if (error) {
      throw boost::system::system_error{error};
   }
   auto ignored = boost::system::error_code{};
   stream.lowest_layer().close(ignored);
   co_return observation;
}

boost::asio::awaitable<void> stcp_direct_roundtrip() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material)};
   auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
   auto pinned_client = client_options(material);
   pinned_client.security.expected_sha256_fingerprint =
       fcl::crypto::x509::certificate::from_pem(material.server.certificate).fingerprint_sha256_text();
   auto connector = fcl::stcp::connector{executor, pinned_client};
   auto client = co_await connector.async_connect_connection(listener.local_endpoint());
   auto server = co_await take_result(accept);

   BOOST_CHECK(client.valid());
   BOOST_CHECK(server.valid());
   BOOST_CHECK_EQUAL(client.selected_alpn(), "fcl-test/1");
   BOOST_CHECK_EQUAL(server.selected_alpn(), "fcl-test/1");
   BOOST_CHECK(client.peer_certificate().has_value());
   BOOST_CHECK(server.peer_certificate().has_value() == false);

   const auto ping = text_bytes("tls ping");
   co_await client.async_write(ping);
   auto received_ping = co_await server.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_ping.begin(), received_ping.end(), ping.begin(), ping.end());

   auto stream_connection = std::move(client).into_transport_stream();
   const auto framed = text_bytes("tls framed");
   co_await stream_connection.stream.async_write_frame(framed);
   auto received_frame = co_await server.async_read();
   BOOST_CHECK(received_frame.size() > framed.size());

   co_await stream_connection.stream.async_close();
   co_await server.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> stcp_upgrade_roundtrip() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::tcp::listener{executor, loopback(0)};
   auto accept = spawn_result<fcl::tcp::connection>(executor, listener.async_accept_connection());
   auto connector = fcl::tcp::connector{executor};
   auto client_tcp = co_await connector.async_connect_connection(listener.local_endpoint());
   auto server_tcp = co_await take_result(accept);

   const auto prelude = text_bytes("clear prelude");
   co_await client_tcp.async_write(prelude);
   auto received_prelude = co_await server_tcp.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received_prelude.begin(), received_prelude.end(), prelude.begin(), prelude.end());

   auto server_upgrade =
       spawn_result<fcl::stcp::connection>(executor,
                                           fcl::stcp::async_upgrade_server(std::move(server_tcp), server_options(material)));
   auto client = co_await fcl::stcp::async_upgrade_client(std::move(client_tcp), client_options(material));
   auto server = co_await take_result(server_upgrade);

   const auto encrypted = text_bytes("encrypted after upgrade");
   co_await server.async_write(encrypted);
   auto received = co_await client.async_read();
   BOOST_CHECK_EQUAL_COLLECTIONS(received.begin(), received.end(), encrypted.begin(), encrypted.end());

   co_await client.async_close();
   co_await server.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> stcp_verification_failures() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;

   {
      auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material)};
      auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
      auto client = client_options(material);
      client.server_name = "wrong.local";
      auto connector = fcl::stcp::connector{executor, client};
      BOOST_CHECK_THROW((void)co_await connector.async_connect_connection(listener.local_endpoint()),
                        fcl::stcp::exceptions::verification_failed);
      listener.cancel();
      try {
         (void)co_await take_result(accept);
      } catch (...) {
      }
   }

   {
      auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material)};
      auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
      const auto wrong_ca = make_tls_material();
      auto client = client_options(material);
      client.security.trusted_ca_pem = wrong_ca.ca.certificate;
      auto connector = fcl::stcp::connector{executor, client};
      BOOST_CHECK_THROW((void)co_await connector.async_connect_connection(listener.local_endpoint()),
                        fcl::stcp::exceptions::verification_failed);
      listener.cancel();
      try {
         (void)co_await take_result(accept);
      } catch (...) {
      }
   }

   {
      auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material)};
      auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
      auto client = client_options(material);
      client.security.expected_sha256_fingerprint = std::string(64, '0');
      auto connector = fcl::stcp::connector{executor, client};
      BOOST_CHECK_THROW((void)co_await connector.async_connect_connection(listener.local_endpoint()),
                        fcl::stcp::exceptions::verification_failed);
      listener.cancel();
      try {
         (void)co_await take_result(accept);
      } catch (...) {
      }
   }
}

boost::asio::awaitable<void> stcp_mutual_tls() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;

   auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material, true)};
   auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
   auto connector = fcl::stcp::connector{executor, client_options(material, true)};
   auto client = co_await connector.async_connect_connection(listener.local_endpoint());
   auto server = co_await take_result(accept);
   BOOST_CHECK(server.peer_certificate().has_value());
   co_await client.async_close();
   co_await server.async_close();
   co_await listener.async_close();

   auto rejecting_listener = fcl::stcp::listener{executor, loopback(0), server_options(material, true)};
   auto rejecting_accept =
       spawn_result<fcl::stcp::connection>(executor, rejecting_listener.async_accept_connection());
   auto no_cert_connector = fcl::stcp::connector{executor, client_options(material, false)};
   auto no_cert_client = std::optional<fcl::stcp::connection>{};
   try {
      no_cert_client.emplace(co_await no_cert_connector.async_connect_connection(rejecting_listener.local_endpoint()));
   } catch (const fcl::stcp::exceptions::handshake_failed&) {
   }
   BOOST_CHECK_THROW((void)co_await take_result(rejecting_accept), fcl::stcp::exceptions::handshake_failed);
   if (no_cert_client) {
      co_await no_cert_client->async_close();
   }
   co_await rejecting_listener.async_close();
}

boost::asio::awaitable<void> stcp_sni_policy() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;

   {
      auto listener = fcl::tcp::listener{executor, loopback(0)};
      auto accept = spawn_result<raw_tls_observation>(executor, accept_raw_tls_once(listener, material));
      auto connector = fcl::tcp::connector{executor};
      auto tcp = co_await connector.async_connect_connection(listener.local_endpoint());
      auto options = fcl::stcp::client_options{};
      options.security.verify_peer = false;
      options.sni = fcl::stcp::sni_policy::disabled;
      auto tls = co_await fcl::stcp::async_upgrade_client(std::move(tcp), options);
      const auto observation = co_await take_result(accept);
      BOOST_TEST(observation.sni.empty());
      co_await tls.async_close();
      co_await listener.async_close();
   }

   {
      auto listener = fcl::tcp::listener{executor, loopback(0)};
      auto accept = spawn_result<raw_tls_observation>(executor, accept_raw_tls_once(listener, material));
      auto connector = fcl::tcp::connector{executor};
      auto tcp = co_await connector.async_connect_connection(listener.local_endpoint());
      auto options = fcl::stcp::client_options{};
      options.security.verify_peer = false;
      options.sni = fcl::stcp::sni_policy::explicit_name;
      options.server_name = "libp2p.example";
      auto tls = co_await fcl::stcp::async_upgrade_client(std::move(tcp), options);
      const auto observation = co_await take_result(accept);
      BOOST_TEST(observation.sni == "libp2p.example");
      co_await tls.async_close();
      co_await listener.async_close();
   }
}

boost::asio::awaitable<void> stcp_verifier_receives_certificate_chain() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;

   auto server = server_options(material, false);
   server.security.require_peer_certificate = true;
   auto saw_chain = std::make_shared<bool>(false);
   auto seen_size = std::make_shared<std::size_t>(0);
   server.security.verifier = [saw_chain, seen_size](const fcl::stcp::certificate_chain& chain) {
      *saw_chain = true;
      *seen_size = chain.certificates.size();
      return !chain.certificates.empty() && !chain.certificates.front().der.empty();
   };

   auto listener = fcl::stcp::listener{executor, loopback(0), server};
   auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
   auto connector = fcl::stcp::connector{executor, client_options(material, true)};
   auto client = co_await connector.async_connect_connection(listener.local_endpoint());
   auto server_connection = co_await take_result(accept);

   BOOST_TEST(*saw_chain);
   BOOST_TEST(*seen_size >= 1U);
   BOOST_TEST(server_connection.peer_certificate_chain().certificates.size() == *seen_size);

   co_await client.async_close();
   co_await server_connection.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> stcp_alpn_uses_client_preference() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;

   auto server = server_options(material);
   server.alpn_protocols = {"muxer2", "/yamux/1.0.0", "libp2p"};
   auto client = client_options(material);
   client.alpn_protocols = {"/yamux/1.0.0", "muxer2", "libp2p"};

   auto listener = fcl::stcp::listener{executor, loopback(0), server};
   auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
   auto connector = fcl::stcp::connector{executor, client};
   auto client_connection = co_await connector.async_connect_connection(listener.local_endpoint());
   auto server_connection = co_await take_result(accept);

   BOOST_TEST(client_connection.selected_alpn() == "/yamux/1.0.0");
   BOOST_TEST(server_connection.selected_alpn() == "/yamux/1.0.0");

   co_await client_connection.async_close();
   co_await server_connection.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> stcp_connection_cancel_unblocks_pending_read() {
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material)};
   auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());
   auto connector = fcl::stcp::connector{executor, client_options(material)};
   auto client = co_await connector.async_connect_connection(listener.local_endpoint());
   auto server = co_await take_result(accept);

   auto timer = boost::asio::steady_timer{executor};
   timer.expires_after(std::chrono::milliseconds{25});
   boost::asio::co_spawn(
       executor,
       [&server, timer = std::move(timer)]() mutable -> boost::asio::awaitable<void> {
          co_await timer.async_wait(boost::asio::use_awaitable);
          server.cancel();
       },
       boost::asio::detached);

   try {
      (void)co_await server.async_read();
      BOOST_FAIL("stcp read should be canceled");
   } catch (const fcl::stcp::exceptions::canceled&) {
   }

   co_await client.async_close();
   co_await listener.async_close();
}

boost::asio::awaitable<void> stcp_rejects_tls12_peer() {
   namespace asio = boost::asio;
   const auto material = make_tls_material();
   auto executor = co_await boost::asio::this_coro::executor;
   auto listener = fcl::stcp::listener{executor, loopback(0), server_options(material)};
   auto accept = spawn_result<fcl::stcp::connection>(executor, listener.async_accept_connection());

   auto socket = boost::asio::ip::tcp::socket{executor};
   co_await socket.async_connect(boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"),
                                                                 listener.local_endpoint().port},
                                 boost::asio::use_awaitable);
   auto context = asio::ssl::context{asio::ssl::context::tls_client};
   SSL_CTX_set_max_proto_version(context.native_handle(), TLS1_2_VERSION);
   context.set_verify_mode(asio::ssl::verify_none);
   auto stream = asio::ssl::stream<boost::asio::ip::tcp::socket>{std::move(socket), context};
   auto error = boost::system::error_code{};
   co_await stream.async_handshake(asio::ssl::stream_base::client,
                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
   BOOST_TEST(error.failed());
   BOOST_CHECK_THROW((void)co_await take_result(accept), fcl::stcp::exceptions::handshake_failed);
   co_await listener.async_close();
}

} // namespace

BOOST_AUTO_TEST_SUITE(stcp)

BOOST_AUTO_TEST_CASE(stcp_loopback_roundtrip_and_transport_stream) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_direct_roundtrip());
}

BOOST_AUTO_TEST_CASE(stcp_upgrades_existing_tcp_connection) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_upgrade_roundtrip());
}

BOOST_AUTO_TEST_CASE(stcp_rejects_wrong_hostname_and_fingerprint) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_verification_failures());
}

BOOST_AUTO_TEST_CASE(stcp_supports_mutual_tls) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_mutual_tls());
}

BOOST_AUTO_TEST_CASE(stcp_controls_sni_explicitly) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_sni_policy());
}

BOOST_AUTO_TEST_CASE(stcp_verifier_receives_full_certificate_chain) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_verifier_receives_certificate_chain());
}

BOOST_AUTO_TEST_CASE(stcp_alpn_selects_client_preferred_supported_protocol) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_alpn_uses_client_preference());
}

BOOST_AUTO_TEST_CASE(stcp_cancel_unblocks_pending_read) {
   auto runtime = fcl::asio::runtime{};
   BOOST_CHECK(fcl::asio::blocking::run_for(runtime, stcp_connection_cancel_unblocks_pending_read(), std::chrono::seconds{2}));
}

BOOST_AUTO_TEST_CASE(stcp_requires_tls13_by_default) {
   auto runtime = fcl::asio::runtime{};
   fcl::asio::blocking::run(runtime, stcp_rejects_tls12_peer());
}

BOOST_AUTO_TEST_SUITE_END()
