#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.quic.connection;
import fcl.quic.connector;
import fcl.quic.endpoint;
import fcl.quic.exceptions;
import fcl.quic.framed_stream;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.runtime;
import fcl.quic.security;

namespace fcl::quic {
namespace {

using udp = boost::asio::ip::udp;

std::string_view test_certificate() {
   return "-----BEGIN CERTIFICATE-----\n"
          "MIICpDCCAYwCCQCJjaEDxrQqBzANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
          "MjcuMC4wLjEwHhcNMjYwNDI5MDgwMTMzWhcNMjYwNDMwMDgwMTMzWjAUMRIwEAYD\n"
          "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDy\n"
          "sbPH/R4QUz725sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TX\n"
          "gl9tHkNpKmI92s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+\n"
          "x7MRWXfKYd/ArGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOS\n"
          "lI/lDqIjZxo7jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuv\n"
          "M+mTj6eO4UQ42w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXj\n"
          "nPOZzBinLRTDnE59HbDZAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHSOUQTEDgjC\n"
          "uwza9ayfThJTs43j+TziWHLlowqCiHt/ipRNFEW7L0ibTnbMdQBFGfaLkTAhc5Rd\n"
          "6O6x+9o76pgEYxEg0rDkgNXmprNmS+nL7Are+iiF6R+X8dts3MQgtONPApAXE96P\n"
          "/n5K4GDQTd3WCI37hkmJA6rmwziFDTlwqtKWts39g8PqAbXac27rVR/iD0gWdOws\n"
          "qiaoGj/0WW9qcgjYGdCc0/CbbnyiWbi48VVf0yyfm7wgcz90byaKIQchHdb/qjyU\n"
          "wy7nfU5TJ5MKQ5yeqPTWmPYZZp9TKa5VD6wZD/IH7jH3GdJ/fSyroVLZktVnmxJa\n"
          "dmG/9wwivwQ=\n"
          "-----END CERTIFICATE-----\n";
}

std::string_view test_private_key() {
   return "-----BEGIN PRIVATE KEY-----\n"
          "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDysbPH/R4QUz72\n"
          "5sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TXgl9tHkNpKmI9\n"
          "2s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+x7MRWXfKYd/A\n"
          "rGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOSlI/lDqIjZxo7\n"
          "jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuvM+mTj6eO4UQ4\n"
          "2w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXjnPOZzBinLRTD\n"
          "nE59HbDZAgMBAAECggEBAIWVjHhy+V5RA+JRCh/12ayirNLG2BF30OP9pf7iL4IT\n"
          "/dMPbKvkmDGLw+1bW8tgKXj5+N6N/trfCm4zhqI3OF7ihooH9qYM88/F/OvMjFiU\n"
          "BhMVVhJW1LxtPPjKUcFN58M8VnMhRM9v6gIaoSOJZvpU1abVtgBDocyJUxAB6gYp\n"
          "i7MzoRwHGsL5mW/luE5H92/S8NNwLWBDA7DIGfrTZ6POf92h5I5W3CuTcqR5FICz\n"
          "3pfU3i443yZmsmkc9duH2gZ9cb9j4pRtNLbbsGmRVrBlgnkVFk8JWbikc8MpLeKO\n"
          "VKP7A2NvxJIrc7oFYrf4hbw8P70YL7S9B3W3yBPPzJECgYEA+Y3nG8CtvVTE/Keo\n"
          "qb5Rljlnj9DEffrylLyYUYfSSNR4Olc2WCPBiz0rPCDdO0VGeXAwqLf2VP7IEyAx\n"
          "kvrnqhzHWMhiLv+k4tIVyKCwpuofN0JsoUCi7CwRf+H2Pg+t6ewLV116THKsd41H\n"
          "IRElWyEvZsmbbhlLrsxUtfFZWnUCgYEA+PZwXUn+cb8kRmfG959gMawTtcfvnBUX\n"
          "sIn7LQl/ZWUIiLMWCaS3FbqkiGjaEYo6om1invYNJNA9zp/ECauSDp58NICCL0ie\n"
          "L7z26sEa6Ocg2VdR4ezpN3cM6dyAKfTFGb9V6qjyqNIPCE4eey6ZJ+CU/mpEfSDu\n"
          "+RGMzfdDCFUCgYEA5FRUn0zk6jU0YyMXq+9pgLSXL7vI/Kdt6m7AQuCto1tbga2o\n"
          "GG7mt/pIo6RCJufUemoO62AeL1hKQU2UbjHJYxkfv/jf9LaM68dijQWRe7b8xres\n"
          "4sFcEBCmFkbt4YzBCCWjntT1gBrv+Ba4fOXOMxoi374Yy1yzpYRpAWuI4L0CgYAn\n"
          "u1SlXrivuHx2i/tR62pzou2mVhkkRK16LBsczeY57UzWXBZJRbM+UYIOjwU2RWQk\n"
          "JebWTZg9ZspmXlLv5CS0FpDl5BhiqWktXy/cuSKtRq2UYf4cWy3A/0vdSqZdi8Wk\n"
          "3Uc94uaPEK77eVQd/orMtWexzo3NlmLs9uMMv8g/3QKBgQCbik0UoJkkqNRMmWG8\n"
          "dKQzj58eRI8fmKdJlWNfj2QMspd2vXMbsWYgAbFbU1QcVs1n8PxNydM+cfy77w8q\n"
          "NWMlYP7rUFQ3ekYWqrRlshZdJ/h24PALd1nPCvhc4C9dvn+zW3BLVez1lBuFO8n8\n"
          "0YkgmTgW7Ieibqnf4DqYp//nkw==\n"
          "-----END PRIVATE KEY-----\n";
}

struct bio_deleter {
   void operator()(BIO* bio) const noexcept {
      BIO_free(bio);
   }
};

struct x509_deleter {
   void operator()(X509* certificate) const noexcept {
      X509_free(certificate);
   }
};

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* key) const noexcept {
      EVP_PKEY_free(key);
   }
};

struct x509_extension_deleter {
   void operator()(X509_EXTENSION* extension) const noexcept {
      X509_EXTENSION_free(extension);
   }
};

struct generated_identity {
   std::string certificate_pem;
   std::string private_key_pem;
};

std::string bio_to_string(BIO* bio) {
   BUF_MEM* memory = nullptr;
   BIO_get_mem_ptr(bio, &memory);
   BOOST_REQUIRE(memory != nullptr);
   return std::string{memory->data, memory->length};
}

void add_certificate_extension(X509* certificate, int nid, std::string_view value) {
   auto context = X509V3_CTX{};
   X509V3_set_ctx_nodb(&context);
   X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
   auto extension = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>{
       X509V3_EXT_conf_nid(nullptr, &context, nid, std::string{value}.c_str())};
   BOOST_REQUIRE(extension != nullptr);
   BOOST_REQUIRE(X509_add_ext(certificate, extension.get(), -1) == 1);
}

generated_identity generate_test_identity(std::string_view subject_alt_name) {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{EVP_RSA_gen(2048)};
   BOOST_REQUIRE(key != nullptr);
   auto certificate = std::unique_ptr<X509, x509_deleter>{X509_new()};
   BOOST_REQUIRE(certificate != nullptr);

   BOOST_REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1);
   BOOST_REQUIRE(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) != nullptr);
   BOOST_REQUIRE(X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 60 * 60) != nullptr);
   BOOST_REQUIRE(X509_set_pubkey(certificate.get(), key.get()) == 1);

   auto* name = X509_get_subject_name(certificate.get());
   BOOST_REQUIRE(name != nullptr);
   const auto common_name = std::string{"fcl-quic-test"};
   BOOST_REQUIRE(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                            reinterpret_cast<const unsigned char*>(common_name.data()),
                                            static_cast<int>(common_name.size()), -1, 0) == 1);
   BOOST_REQUIRE(X509_set_issuer_name(certificate.get(), name) == 1);
   add_certificate_extension(certificate.get(), NID_basic_constraints, "critical,CA:TRUE");
   add_certificate_extension(certificate.get(), NID_subject_alt_name, subject_alt_name);
   BOOST_REQUIRE(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0);

   auto certificate_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   auto private_key_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   BOOST_REQUIRE(certificate_bio != nullptr);
   BOOST_REQUIRE(private_key_bio != nullptr);
   BOOST_REQUIRE(PEM_write_bio_X509(certificate_bio.get(), certificate.get()) == 1);
   BOOST_REQUIRE(PEM_write_bio_PrivateKey(private_key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) ==
                 1);
   return generated_identity{
       .certificate_pem = bio_to_string(certificate_bio.get()),
       .private_key_pem = bio_to_string(private_key_bio.get()),
   };
}

struct scoped_quic_connect_failpoint {
   explicit scoped_quic_connect_failpoint(std::string_view name) {
      ::setenv("STORLANE_NETWORK_QUIC_CONNECT_FAILPOINT", std::string{name}.c_str(), 1);
   }

   ~scoped_quic_connect_failpoint() {
      ::unsetenv("STORLANE_NETWORK_QUIC_CONNECT_FAILPOINT");
   }
};

std::vector<std::uint8_t> test_certificate_der() {
   auto bio = std::unique_ptr<BIO, bio_deleter>{
       BIO_new_mem_buf(test_certificate().data(), static_cast<int>(test_certificate().size()))};
   BOOST_REQUIRE(bio != nullptr);
   auto certificate = std::unique_ptr<X509, x509_deleter>{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
   BOOST_REQUIRE(certificate != nullptr);

   const auto len = i2d_X509(certificate.get(), nullptr);
   BOOST_REQUIRE(len > 0);
   auto der = std::vector<std::uint8_t>(static_cast<std::size_t>(len));
   auto* out = der.data();
   BOOST_REQUIRE(i2d_X509(certificate.get(), &out) == len);
   return der;
}

server_options loopback_server_options(std::string alpn = "fcl-p2p/1", transport_limits limits = {}) {
   return server_options{
       .alpn = std::move(alpn),
       .limits = limits,
       .security = security_options{.verify_peer = false},
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
   };
}

client_options loopback_client_options(std::string alpn = "fcl-p2p/1", transport_limits limits = {}) {
   return client_options{
       .alpn = std::move(alpn),
       .handshake_timeout = std::chrono::milliseconds{5'000},
       .limits = limits,
       .security = security_options{.verify_peer = false},
   };
}

udp::endpoint to_udp_endpoint(const endpoint& value) {
   return udp::endpoint{boost::asio::ip::make_address(value.host), value.port};
}

endpoint to_quic_endpoint(const udp::endpoint& value) {
   using address_member = boost::asio::ip::address (udp::endpoint::*)() const;
   const auto host = std::invoke(static_cast<address_member>(&udp::endpoint::address), value).to_string();
   return endpoint{.host = host, .port = value.port()};
}

template <typename T>
T get_with_deadline(std::future<T>& future, std::chrono::milliseconds timeout, std::string_view label) {
   if (future.wait_for(timeout) != std::future_status::ready) {
      BOOST_FAIL(std::string{"timed out waiting for "} + std::string{label});
      throw std::runtime_error{std::string{"timed out waiting for "} + std::string{label}};
   }
   return future.get();
}

void get_with_deadline(std::future<void>& future, std::chrono::milliseconds timeout, std::string_view label) {
   if (future.wait_for(timeout) != std::future_status::ready) {
      BOOST_FAIL(std::string{"timed out waiting for "} + std::string{label});
      throw std::runtime_error{std::string{"timed out waiting for "} + std::string{label}};
   }
   future.get();
}

template <typename Awaitable>
auto run_with_deadline(fcl::asio::runtime& runtime, Awaitable&& awaitable, std::chrono::milliseconds timeout,
                       std::string_view label) {
   auto future = boost::asio::co_spawn(runtime.context(), std::forward<Awaitable>(awaitable), boost::asio::use_future);
   return get_with_deadline(future, timeout, label);
}

struct fault_rule {
   std::uint32_t drop_every = 0;
   std::uint32_t drop_after = 0;
   std::uint32_t duplicate_every = 0;
   std::uint32_t duplicate_after = 0;
   std::uint32_t reorder_every = 0;
   std::uint32_t reorder_after = 0;
   std::chrono::milliseconds delay{0};
};

struct fault_proxy_rules {
   fault_rule client_to_server;
   fault_rule server_to_client;
};

struct fault_direction_metrics {
   std::uint64_t received = 0;
   std::uint64_t sent = 0;
   std::uint64_t dropped = 0;
   std::uint64_t duplicated = 0;
   std::uint64_t delayed = 0;
   std::uint64_t reordered = 0;
};

struct fault_proxy_metrics {
   fault_direction_metrics client_to_server;
   fault_direction_metrics server_to_client;
};

class udp_fault_proxy : public std::enable_shared_from_this<udp_fault_proxy> {
 public:
   udp_fault_proxy(boost::asio::io_context& context, endpoint server_endpoint, fault_proxy_rules rules)
       : socket_(context, udp::endpoint{boost::asio::ip::make_address("127.0.0.1"), 0}),
         server_endpoint_(to_udp_endpoint(server_endpoint)), rules_(rules) {}

   ~udp_fault_proxy() {
      stop();
   }

   udp_fault_proxy(const udp_fault_proxy&) = delete;
   udp_fault_proxy& operator=(const udp_fault_proxy&) = delete;

   [[nodiscard]] endpoint local_endpoint() const {
      return to_quic_endpoint(socket_.local_endpoint());
   }

   [[nodiscard]] fault_proxy_metrics metrics() const {
      return metrics_;
   }

   void start() {
      do_receive();
   }

   void stop() {
      if (stopped_) {
         return;
      }
      stopped_ = true;
      boost::system::error_code ignored;
      socket_.cancel(ignored);
      socket_.close(ignored);
   }

 private:
   struct packet {
      std::vector<std::uint8_t> bytes;
      udp::endpoint destination;
      bool to_server = true;
   };

   struct direction_state {
      std::uint64_t sequence = 0;
      std::optional<packet> reordered;
   };

   void do_receive() {
      auto self = shared_from_this();
      socket_.async_receive_from(boost::asio::buffer(buffer_), source_endpoint_,
                                 [self](boost::system::error_code ec, std::size_t bytes) {
                                    if (ec || self->stopped_) {
                                       return;
                                    }
                                    self->handle_packet(bytes);
                                    self->do_receive();
                                 });
   }

   void handle_packet(std::size_t bytes) {
      const auto from_server = source_endpoint_ == server_endpoint_;
      if (!from_server) {
         client_endpoint_ = source_endpoint_;
         has_client_endpoint_ = true;
      }
      if (from_server && !has_client_endpoint_) {
         return;
      }

      auto packet_value = packet{
          .bytes = std::vector<std::uint8_t>{buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(bytes)},
          .destination = from_server ? client_endpoint_ : server_endpoint_,
          .to_server = !from_server,
      };
      route_packet(std::move(packet_value));
   }

   void route_packet(packet packet_value) {
      auto& state = packet_value.to_server ? client_to_server_ : server_to_client_;
      auto& metrics = packet_value.to_server ? metrics_.client_to_server : metrics_.server_to_client;
      const auto& rule = packet_value.to_server ? rules_.client_to_server : rules_.server_to_client;
      ++state.sequence;
      ++metrics.received;

      if (rule.drop_every > 0 && state.sequence > rule.drop_after && state.sequence % rule.drop_every == 0) {
         ++metrics.dropped;
         return;
      }

      const auto duplicate = rule.duplicate_every > 0 && state.sequence > rule.duplicate_after &&
                             state.sequence % rule.duplicate_every == 0;
      if (rule.reorder_every > 0 && state.sequence > rule.reorder_after && state.sequence % rule.reorder_every == 0) {
         if (!state.reordered) {
            ++metrics.reordered;
            state.reordered = std::move(packet_value);
            schedule_reorder_flush(state, metrics,
                                   rule.delay == std::chrono::milliseconds{0} ? std::chrono::milliseconds{5}
                                                                              : rule.delay * 2);
            return;
         }
      }

      send_or_delay(packet_value, rule, metrics);
      if (duplicate) {
         ++metrics.duplicated;
         send_or_delay(packet{.bytes = packet_value.bytes,
                              .destination = packet_value.destination,
                              .to_server = packet_value.to_server},
                       rule, metrics);
      }
      if (state.reordered) {
         auto held = std::move(*state.reordered);
         state.reordered.reset();
         send_or_delay(std::move(held), rule, metrics);
      }
   }

   void schedule_reorder_flush(direction_state& state, fault_direction_metrics& metrics,
                               std::chrono::milliseconds delay) {
      auto timer = std::make_shared<boost::asio::steady_timer>(socket_.get_executor());
      timer->expires_after(delay);
      auto self = shared_from_this();
      auto* state_ptr = &state;
      auto* metrics_ptr = &metrics;
      timer->async_wait([self, timer, state_ptr, metrics_ptr](boost::system::error_code ec) {
         if (ec || self->stopped_ || !state_ptr->reordered) {
            return;
         }
         auto held = std::move(*state_ptr->reordered);
         state_ptr->reordered.reset();
         self->send_now(std::move(held), *metrics_ptr);
      });
   }

   void send_or_delay(packet packet_value, const fault_rule& rule, fault_direction_metrics& metrics) {
      if (rule.delay == std::chrono::milliseconds{0}) {
         send_now(std::move(packet_value), metrics);
         return;
      }
      ++metrics.delayed;
      auto timer = std::make_shared<boost::asio::steady_timer>(socket_.get_executor());
      timer->expires_after(rule.delay);
      auto self = shared_from_this();
      auto metrics_ptr = &metrics;
      timer->async_wait(
          [self, timer, packet_value = std::move(packet_value), metrics_ptr](boost::system::error_code ec) mutable {
             if (ec || self->stopped_) {
                return;
             }
             self->send_now(std::move(packet_value), *metrics_ptr);
          });
   }

   void send_now(packet packet_value, fault_direction_metrics& metrics) {
      auto self = shared_from_this();
      auto payload = std::make_shared<std::vector<std::uint8_t>>(std::move(packet_value.bytes));
      auto* metrics_ptr = &metrics;
      socket_.async_send_to(boost::asio::buffer(*payload), packet_value.destination,
                            [self, payload, metrics_ptr](boost::system::error_code ec, std::size_t) {
                               if (!ec) {
                                  ++metrics_ptr->sent;
                               }
                            });
   }

   udp::socket socket_;
   udp::endpoint server_endpoint_;
   udp::endpoint client_endpoint_;
   udp::endpoint source_endpoint_;
   bool has_client_endpoint_ = false;
   bool stopped_ = false;
   std::array<std::uint8_t, 64 * 1024> buffer_{};
   fault_proxy_rules rules_;
   direction_state client_to_server_;
   direction_state server_to_client_;
   fault_proxy_metrics metrics_;
};

BOOST_AUTO_TEST_CASE(quic_endpoint_parses_ipv4_authority) {
   const auto host = std::string{"127.0.0.1"};
   const auto authority = host + ":" + std::to_string(9443);
   const auto value = parse_endpoint(std::string{"quic"} + "://" + authority);

   BOOST_TEST(value.host == host);
   BOOST_TEST(value.port == 9443);
   BOOST_TEST(value.authority() == authority);
}

BOOST_AUTO_TEST_CASE(quic_endpoint_parses_bracketed_ipv6_authority) {
   const auto value = parse_endpoint(std::string{"quic"} + "://[::1]:" + std::to_string(9443));

   BOOST_TEST(value.host == "::1");
   BOOST_TEST(value.port == 9443);
}

BOOST_AUTO_TEST_CASE(quic_endpoint_rejects_non_quic_scheme) {
   try {
      (void)parse_endpoint(std::string{"https"} + "://" + std::string{"127.0.0.1"} + ":" + std::to_string(9443));
      BOOST_FAIL("expected typed QUIC exception");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::invalid_endpoint));
   }
}

BOOST_AUTO_TEST_CASE(quic_connect_timeout_wins_over_pre_connection_error_race) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto client = connector{runtime};
   auto failpoint = scoped_quic_connect_failpoint{"timeout_before_pre_connection_error_finish"};
   static_cast<void>(failpoint);

   try {
      (void)run_with_deadline(
          runtime,
          client.async_connect(endpoint{.host = "not a valid host name", .port = 443}, loopback_client_options()),
          std::chrono::milliseconds{2'000}, "pre-connection error timeout winner");
      BOOST_FAIL("expected QUIC connect timeout");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::connect_timeout));
   }
}

BOOST_AUTO_TEST_CASE(quic_frame_codec_round_trips_payload) {
   const auto payload = std::vector<std::uint8_t>{1, 2, 3, 4, 5};
   const auto encoded = encode_frame(payload);
   const auto decoded = decode_frame(encoded);

   BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(frame_decode_status::complete));
   BOOST_TEST(decoded.consumed == encoded.size());
   BOOST_TEST(decoded.payload == payload, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(quic_frame_codec_reports_need_more_data) {
   const auto payload = std::vector<std::uint8_t>{1, 2, 3};
   auto encoded = encode_frame(payload);
   encoded.pop_back();

   const auto decoded = decode_frame(encoded);

   BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(frame_decode_status::need_more_data));
   BOOST_TEST(decoded.consumed == 0U);
}

BOOST_AUTO_TEST_CASE(quic_frame_codec_rejects_oversized_payload) {
   const auto payload = std::vector<std::uint8_t>{1, 2, 3, 4};

   try {
      (void)encode_frame(payload, frame_codec_options{.max_frame_size = 3});
      BOOST_FAIL("expected typed QUIC exception");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::frame_too_large));
   }
}

BOOST_AUTO_TEST_CASE(quic_security_normalizes_fingerprint) {
   const auto raw =
       std::string{"AA:BB:CC:DD:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB"};
   const auto normalized = normalize_sha256_fingerprint(raw);

   BOOST_TEST(normalized == "aabbccdd00112233445566778899aabbccddeeff00112233445566778899aabb");
}

BOOST_AUTO_TEST_CASE(quic_security_verifies_pinned_certificate_digest) {
   const auto der = std::vector<std::uint8_t>{'s', 't', 'o', 'r', 'l', 'a', 'n', 'e'};
   const auto digest = sha256_fingerprint(der);
   const auto certificate = peer_certificate{.der = der, .sha256_fingerprint = digest};
   const auto options = security_options{.verify_peer = true, .expected_sha256_fingerprint = digest};

   BOOST_TEST(verify_peer_certificate(certificate, options));
}

BOOST_AUTO_TEST_CASE(quic_options_validation_rejects_bad_alpn) {
   auto options = client_options{};
   options.alpn.clear();

   try {
      validate(options);
      BOOST_FAIL("expected typed QUIC exception");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::invalid_options));
   }
}

BOOST_AUTO_TEST_CASE(quic_runtime_initializes_ngtcp2_crypto_ossl) {
   const auto capabilities = initialize_runtime();

   BOOST_TEST(!capabilities.ngtcp2_version.empty());
   BOOST_TEST(capabilities.tls_backend == "openssl");
   BOOST_TEST(capabilities.openssl_version_major >= 3U);
   BOOST_TEST(capabilities.crypto_ossl_initialized);
}

BOOST_AUTO_TEST_CASE(quic_loopback_handshake_and_echo_frame_over_udp) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{
       runtime,
       endpoint{.host = "127.0.0.1", .port = 0},
       loopback_server_options(),
   };

   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   auto server_connection = accept_future.get();

   auto server_echo = boost::asio::co_spawn(
       runtime.context(),
       [server_connection =
            std::move(server_connection)]() mutable -> boost::asio::awaitable<std::vector<std::uint8_t>> {
          auto accepted = co_await server_connection.async_accept_stream();
          auto framed = framed_stream{std::move(accepted)};
          auto request = co_await framed.async_read_frame();
          co_await framed.async_write_frame(request);
          co_return request;
       },
       boost::asio::use_future);

   auto client_stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
   auto framed = framed_stream{std::move(client_stream)};
   const auto payload = std::vector<std::uint8_t>{'p', 'i', 'n', 'g'};
   fcl::asio::blocking::run(runtime, framed.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, framed.async_read_frame());
   const auto server_seen = server_echo.get();

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(server_seen == payload, boost::test_tools::per_element());
   BOOST_TEST(client_connection.metrics().handshakes_completed >= 1U);
   BOOST_TEST(client_connection.metrics().streams_opened >= 1U);

   fcl::asio::blocking::run(runtime, client_connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_medium_frame_and_small_frame_burst) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   auto server_connection = accept_future.get();

   constexpr auto small_frame_count = 10'000U;
   auto server_task = boost::asio::co_spawn(
       runtime.context(),
       [server_connection =
            std::move(server_connection)]() mutable -> boost::asio::awaitable<std::pair<std::size_t, std::size_t>> {
          auto accepted = co_await server_connection.async_accept_stream();
          auto framed = framed_stream{std::move(accepted)};
          auto large = co_await framed.async_read_frame();
          co_await framed.async_write_frame(large);

          auto small_seen = std::size_t{0};
          for (auto index = 0U; index < small_frame_count; ++index) {
             auto frame = co_await framed.async_read_frame();
             small_seen += frame.size();
          }
          co_return std::pair{large.size(), small_seen};
       },
       boost::asio::use_future);

   auto client_stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
   auto framed = framed_stream{std::move(client_stream)};
   auto large_payload = std::vector<std::uint8_t>(256 * 1024);
   for (std::size_t index = 0; index < large_payload.size(); ++index) {
      large_payload[index] = static_cast<std::uint8_t>(index % 251U);
   }
   fcl::asio::blocking::run(runtime, framed.async_write_frame(large_payload));
   const auto large_reply = fcl::asio::blocking::run(runtime, framed.async_read_frame());
   BOOST_TEST(large_reply == large_payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, [&framed]() -> boost::asio::awaitable<void> {
      for (auto index = 0U; index < small_frame_count; ++index) {
         const auto payload = std::vector<std::uint8_t>{static_cast<std::uint8_t>(index & 0xffU)};
         co_await framed.async_write_frame(payload);
      }
   }());
   const auto [large_seen, small_seen] = server_task.get();

   BOOST_TEST(large_seen == large_payload.size());
   BOOST_TEST(small_seen == small_frame_count);
   BOOST_TEST(client_connection.metrics().frames_sent >= small_frame_count + 1U);
   BOOST_TEST(client_connection.metrics().backpressure_rejections == 0U);

   fcl::asio::blocking::run(runtime, client_connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_large_frame_over_real_quic_stream) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   auto server_connection = accept_future.get();

   auto server_echo = boost::asio::co_spawn(
       runtime.context(),
       [server_connection = std::move(server_connection)]() mutable -> boost::asio::awaitable<std::size_t> {
          auto accepted = co_await server_connection.async_accept_stream();
          auto framed = framed_stream{std::move(accepted)};
          auto request = co_await framed.async_read_frame();
          const auto size = request.size();
          co_await framed.async_write_frame(request);
          co_return size;
       },
       boost::asio::use_future);

   auto client_stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
   auto framed = framed_stream{std::move(client_stream)};
   auto payload = std::vector<std::uint8_t>(4 * 1024 * 1024);
   for (std::size_t index = 0; index < payload.size(); ++index) {
      payload[index] = static_cast<std::uint8_t>((index * 17U) % 251U);
   }
   fcl::asio::blocking::run(runtime, framed.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, framed.async_read_frame());
   const auto server_seen = server_echo.get();

   BOOST_TEST(server_seen == payload.size());
   BOOST_TEST(reply.size() == payload.size());
   BOOST_TEST(std::equal(reply.begin(), reply.end(), payload.begin(), payload.end()));
   BOOST_TEST(client_connection.metrics().backpressure_rejections == 0U);

   fcl::asio::blocking::run(runtime, client_connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_many_parallel_streams_echo_frames) {
   auto limits =
       transport_limits{.max_connections = 16, .max_streams_per_connection = 64, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection = fcl::asio::blocking::run(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", limits)));
   auto server_connection = accept_future.get();

   constexpr auto stream_count = 32U;
   auto server_echo = boost::asio::co_spawn(
       runtime.context(),
       [server_connection = std::move(server_connection)]() mutable -> boost::asio::awaitable<std::size_t> {
          auto total = std::size_t{0};
          for (auto index = 0U; index < stream_count; ++index) {
             auto accepted = co_await server_connection.async_accept_stream();
             auto framed = framed_stream{std::move(accepted)};
             auto request = co_await framed.async_read_frame();
             total += request.size();
             co_await framed.async_write_frame(request);
          }
          co_return total;
       },
       boost::asio::use_future);

   auto replies = fcl::asio::blocking::run(
       runtime, [&client_connection]() mutable -> boost::asio::awaitable<std::vector<std::vector<std::uint8_t>>> {
          auto streams = std::vector<framed_stream>{};
          auto expected = std::vector<std::vector<std::uint8_t>>{};
          streams.reserve(stream_count);
          expected.reserve(stream_count);
          for (auto index = 0U; index < stream_count; ++index) {
             auto stream_value = co_await client_connection.async_open_stream();
             streams.emplace_back(framed_stream{std::move(stream_value)});
             auto payload = std::vector<std::uint8_t>(128U + index);
             std::fill(payload.begin(), payload.end(), static_cast<std::uint8_t>(index));
             co_await streams.back().async_write_frame(payload);
             expected.push_back(std::move(payload));
          }

          auto replies = std::vector<std::vector<std::uint8_t>>{};
          replies.reserve(stream_count);
          for (auto& stream_value : streams) {
             replies.push_back(co_await stream_value.async_read_frame());
          }
          co_return replies;
       }());
   const auto server_total = server_echo.get();

   auto expected_total = std::size_t{0};
   for (auto index = 0U; index < stream_count; ++index) {
      expected_total += 128U + index;
      BOOST_TEST(replies[index].size() == 128U + index);
      BOOST_TEST(std::all_of(replies[index].begin(), replies[index].end(),
                             [index](std::uint8_t value) { return value == static_cast<std::uint8_t>(index); }));
   }
   BOOST_TEST(server_total == expected_total);
   BOOST_TEST(client_connection.metrics().streams_opened == stream_count);
   BOOST_TEST(client_connection.metrics().backpressure_rejections == 0U);

   fcl::asio::blocking::run(runtime, client_connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_repeated_connect_transfer_close_soak) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};

   constexpr auto iteration_count = 10U;
   for (auto iteration = 0U; iteration < iteration_count; ++iteration) {
      auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
      auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
      auto client = connector{runtime};
      auto client_connection =
          fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
      auto server_connection = accept_future.get();

      auto server_echo = boost::asio::co_spawn(
          runtime.context(),
          [server_connection = std::move(server_connection)]() mutable -> boost::asio::awaitable<void> {
             auto accepted = co_await server_connection.async_accept_stream();
             auto framed = framed_stream{std::move(accepted)};
             auto request = co_await framed.async_read_frame();
             co_await framed.async_write_frame(request);
          },
          boost::asio::use_future);

      auto client_stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
      auto framed = framed_stream{std::move(client_stream)};
      const auto payload = std::vector<std::uint8_t>{static_cast<std::uint8_t>(iteration), 1, 2, 3};
      fcl::asio::blocking::run(runtime, framed.async_write_frame(payload));
      const auto reply = fcl::asio::blocking::run(runtime, framed.async_read_frame());
      server_echo.get();

      BOOST_TEST(reply == payload, boost::test_tools::per_element());
      fcl::asio::blocking::run(runtime, client_connection.async_close());
      server.stop();
   }
}

BOOST_AUTO_TEST_CASE(quic_fault_proxy_handshake_survives_mild_loss) {
   auto limits =
       transport_limits{.max_connections = 16, .max_streams_per_connection = 16, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto proxy = std::make_shared<udp_fault_proxy>(
       runtime.context(), server.local_endpoint(),
       fault_proxy_rules{
           .client_to_server = fault_rule{.drop_every = 7, .delay = std::chrono::milliseconds{1}},
           .server_to_client = fault_rule{.drop_every = 7, .delay = std::chrono::milliseconds{1}},
       });
   proxy->start();

   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection = run_with_deadline(
       runtime, client.async_connect(proxy->local_endpoint(), loopback_client_options("fcl-p2p/1", limits)),
       std::chrono::milliseconds{10'000}, "lossy handshake connect");
   auto server_connection =
       get_with_deadline(accept_future, std::chrono::milliseconds{10'000}, "lossy handshake accept");
   const auto proxy_metrics = proxy->metrics();

   BOOST_TEST(client_connection.valid());
   BOOST_TEST(server_connection.valid());
   BOOST_TEST(proxy_metrics.client_to_server.dropped + proxy_metrics.server_to_client.dropped > 0U);

   run_with_deadline(runtime, client_connection.async_close(), std::chrono::milliseconds{5'000},
                     "lossy handshake close");
   proxy->stop();
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_fault_proxy_framed_echo_survives_loss_delay_reorder_duplicate) {
   auto limits =
       transport_limits{.max_connections = 16, .max_streams_per_connection = 16, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto proxy = std::make_shared<udp_fault_proxy>(runtime.context(), server.local_endpoint(),
                                                  fault_proxy_rules{
                                                      .client_to_server =
                                                          fault_rule{
                                                              .drop_every = 53,
                                                              .drop_after = 16,
                                                              .duplicate_every = 17,
                                                              .duplicate_after = 8,
                                                              .reorder_every = 23,
                                                              .reorder_after = 8,
                                                              .delay = std::chrono::milliseconds{1},
                                                          },
                                                      .server_to_client =
                                                          fault_rule{
                                                              .drop_every = 59,
                                                              .drop_after = 16,
                                                              .duplicate_every = 19,
                                                              .duplicate_after = 8,
                                                              .reorder_every = 29,
                                                              .reorder_after = 8,
                                                              .delay = std::chrono::milliseconds{1},
                                                          },
                                                  });
   proxy->start();

   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection = run_with_deadline(
       runtime, client.async_connect(proxy->local_endpoint(), loopback_client_options("fcl-p2p/1", limits)),
       std::chrono::milliseconds{10'000}, "lossy echo connect");
   auto server_connection = get_with_deadline(accept_future, std::chrono::milliseconds{10'000}, "lossy echo accept");

   auto server_echo = boost::asio::co_spawn(
       runtime.context(),
       [server_connection = std::move(server_connection)]() mutable -> boost::asio::awaitable<std::size_t> {
          auto accepted = co_await server_connection.async_accept_stream();
          auto framed = framed_stream{std::move(accepted)};
          auto request = co_await framed.async_read_frame();
          const auto size = request.size();
          co_await framed.async_write_frame(request);
          co_return size;
       },
       boost::asio::use_future);

   auto client_stream = run_with_deadline(runtime, client_connection.async_open_stream(),
                                          std::chrono::milliseconds{5'000}, "lossy echo open stream");
   auto framed = framed_stream{std::move(client_stream)};
   auto payload = std::vector<std::uint8_t>(64 * 1024);
   for (std::size_t index = 0; index < payload.size(); ++index) {
      payload[index] = static_cast<std::uint8_t>((index * 23U) % 251U);
   }
   run_with_deadline(runtime, framed.async_write_frame(payload), std::chrono::milliseconds{10'000},
                     "lossy echo write frame");
   const auto reply = run_with_deadline(runtime, framed.async_read_frame(), std::chrono::milliseconds{10'000},
                                        "lossy echo read frame");
   const auto server_seen = get_with_deadline(server_echo, std::chrono::milliseconds{10'000}, "lossy echo server task");
   const auto proxy_metrics = proxy->metrics();

   BOOST_TEST(server_seen == payload.size());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(proxy_metrics.client_to_server.dropped + proxy_metrics.server_to_client.dropped > 0U);
   BOOST_TEST(proxy_metrics.client_to_server.reordered + proxy_metrics.server_to_client.reordered > 0U);
   BOOST_TEST(proxy_metrics.client_to_server.duplicated + proxy_metrics.server_to_client.duplicated > 0U);
   BOOST_TEST(proxy_metrics.client_to_server.delayed > 0U);
   BOOST_TEST(proxy_metrics.server_to_client.delayed > 0U);
   BOOST_TEST(client_connection.metrics().backpressure_rejections == 0U);

   run_with_deadline(runtime, client_connection.async_close(), std::chrono::milliseconds{5'000}, "lossy echo close");
   proxy->stop();
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_fault_proxy_repeated_connect_transfer_close) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};

   constexpr auto iteration_count = 3U;
   auto client = connector{runtime};
   for (auto iteration = 0U; iteration < iteration_count; ++iteration) {
      const auto label_prefix = std::string{"lossy reconnect iteration "} + std::to_string(iteration) + " ";
      auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
      auto proxy = std::make_shared<udp_fault_proxy>(runtime.context(), server.local_endpoint(),
                                                     fault_proxy_rules{
                                                         .client_to_server =
                                                             fault_rule{
                                                                 .drop_every = 23,
                                                                 .drop_after = 16,
                                                                 .delay = std::chrono::milliseconds{1},
                                                             },
                                                         .server_to_client =
                                                             fault_rule{
                                                                 .drop_every = 29,
                                                                 .drop_after = 16,
                                                                 .delay = std::chrono::milliseconds{1},
                                                             },
                                                     });
      proxy->start();
      auto server_task = boost::asio::co_spawn(
          runtime.context(),
          [&server]() -> boost::asio::awaitable<std::size_t> {
             auto server_connection = co_await server.async_accept();
             auto accepted = co_await server_connection.async_accept_stream();
             auto framed = framed_stream{std::move(accepted)};
             auto request = co_await framed.async_read_frame();
             const auto size = request.size();
             co_await framed.async_write_frame(request);
             co_return size;
          },
          boost::asio::use_future);

      auto client_connection =
          run_with_deadline(runtime, client.async_connect(proxy->local_endpoint(), loopback_client_options()),
                            std::chrono::milliseconds{10'000}, label_prefix + "connect");
      auto client_stream = run_with_deadline(runtime, client_connection.async_open_stream(),
                                             std::chrono::milliseconds{5'000}, label_prefix + "open stream");
      auto framed = framed_stream{std::move(client_stream)};
      auto payload = std::vector<std::uint8_t>(64 * 1024);
      for (std::size_t index = 0; index < payload.size(); ++index) {
         payload[index] = static_cast<std::uint8_t>((index + iteration * 11U) % 251U);
      }
      run_with_deadline(runtime, framed.async_write_frame(payload), std::chrono::milliseconds{10'000},
                        label_prefix + "write frame");
      const auto reply = run_with_deadline(runtime, framed.async_read_frame(), std::chrono::milliseconds{10'000},
                                           label_prefix + "read frame");
      BOOST_TEST(reply == payload, boost::test_tools::per_element());
      run_with_deadline(runtime, client_connection.async_close(), std::chrono::milliseconds{5'000},
                        label_prefix + "close");
      BOOST_TEST(get_with_deadline(server_task, std::chrono::milliseconds{10'000}, label_prefix + "server task") ==
                 payload.size());
      const auto proxy_metrics = proxy->metrics();
      BOOST_TEST(proxy_metrics.client_to_server.dropped + proxy_metrics.server_to_client.dropped > 0U);
      proxy->stop();
      server.stop();
   }
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_alpn_mismatch) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1")};
   auto client = connector{runtime};

   try {
      (void)fcl::asio::blocking::run(
          runtime,
          client.async_connect(server.local_endpoint(), client_options{
                                                            .alpn = "wrong-alpn",
                                                            .handshake_timeout = std::chrono::milliseconds{500},
                                                            .security = security_options{.verify_peer = false},
                                                        }));
      BOOST_FAIL("expected QUIC handshake/alpn failure");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::handshake_timeout ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::alpn_mismatch || fcl::quic::exceptions::code_of(error).value() == exceptions::code::internal;
      BOOST_TEST(acceptable);
   }
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_connect_timeout_limits_stalled_handshake_budget) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto blackhole = udp::socket{runtime.context()};
   blackhole.open(udp::v4());
   blackhole.bind(udp::endpoint{boost::asio::ip::make_address("127.0.0.1"), 0});

   auto client = connector{runtime};
   auto options = loopback_client_options();
   options.connect_timeout = std::chrono::milliseconds{100};
   options.handshake_timeout = std::chrono::milliseconds{5'000};
   const auto started = std::chrono::steady_clock::now();

   try {
      (void)run_with_deadline(runtime,
                              client.async_connect(to_quic_endpoint(blackhole.local_endpoint()), std::move(options)),
                              std::chrono::milliseconds{2'000}, "blackhole connect timeout");
      BOOST_FAIL("expected QUIC connect timeout");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::connect_timeout));
   }
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   BOOST_TEST(elapsed.count() < 2'000);
   blackhole.close();
}

BOOST_AUTO_TEST_CASE(quic_failed_handshake_releases_listener_connection_slot) {
   auto limits =
       transport_limits{.max_connections = 1, .max_streams_per_connection = 16, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto client = connector{runtime};

   try {
      (void)run_with_deadline(runtime,
                              client.async_connect(server.local_endpoint(),
                                                   client_options{
                                                       .alpn = "wrong-alpn",
                                                       .handshake_timeout = std::chrono::milliseconds{500},
                                                       .limits = limits,
                                                       .security = security_options{.verify_peer = false},
                                                   }),
                              std::chrono::milliseconds{2'000}, "failed alpn connect");
      BOOST_FAIL("expected QUIC ALPN failure");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::handshake_timeout ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::alpn_mismatch || fcl::quic::exceptions::code_of(error).value() == exceptions::code::internal;
      BOOST_TEST(acceptable);
   }

   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto valid = run_with_deadline(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", limits)),
       std::chrono::milliseconds{5'000}, "valid connect after failed alpn");
   auto accepted = get_with_deadline(accept_future, std::chrono::milliseconds{5'000}, "accept after failed alpn");
   BOOST_TEST(valid.valid());
   BOOST_TEST(accepted.valid());

   run_with_deadline(runtime, valid.async_close(), std::chrono::milliseconds{5'000}, "close valid after failed alpn");
   run_with_deadline(runtime, accepted.async_close(), std::chrono::milliseconds{5'000},
                     "close accepted after failed alpn");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_remote_close_during_active_read_is_reported) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   auto server_connection = accept_future.get();

   auto server_close = boost::asio::co_spawn(
       runtime.context(),
       [server_connection = std::move(server_connection)]() mutable -> boost::asio::awaitable<void> {
          auto accepted = co_await server_connection.async_accept_stream();
          auto framed = framed_stream{std::move(accepted)};
          (void)co_await framed.async_read_frame();
          co_await framed.async_close();
       },
       boost::asio::use_future);

   auto client_stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
   auto framed = framed_stream{std::move(client_stream)};
   fcl::asio::blocking::run(runtime, framed.async_write_frame(std::vector<std::uint8_t>{'c', 'l', 'o', 's', 'e'}));
   try {
      (void)fcl::asio::blocking::run(runtime, framed.async_read_frame());
      BOOST_FAIL("expected remote stream close to unblock read with typed error");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::stream_closed ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::connection_closed || fcl::quic::exceptions::code_of(error).value() == exceptions::code::stream_reset;
      BOOST_TEST(acceptable);
   }
   server_close.get();
   fcl::asio::blocking::run(runtime, client_connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_listener_reuses_connection_slot_after_close) {
   auto limits =
       transport_limits{.max_connections = 1, .max_streams_per_connection = 16, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto client = connector{runtime};

   auto accept_first = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto first = run_with_deadline(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", limits)),
       std::chrono::milliseconds{5'000}, "first connect with max one connection");
   auto first_server =
       get_with_deadline(accept_first, std::chrono::milliseconds{5'000}, "first accept with max one connection");
   run_with_deadline(runtime, first.async_close(), std::chrono::milliseconds{5'000}, "first client close");
   run_with_deadline(runtime, first_server.async_close(), std::chrono::milliseconds{5'000}, "first server close");

   auto accept_second = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto second = run_with_deadline(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", limits)),
       std::chrono::milliseconds{5'000}, "second connect after cleanup");
   auto second_server =
       get_with_deadline(accept_second, std::chrono::milliseconds{5'000}, "second accept after cleanup");
   BOOST_TEST(second.valid());
   BOOST_TEST(second_server.valid());

   run_with_deadline(runtime, second.async_close(), std::chrono::milliseconds{5'000}, "second client close");
   run_with_deadline(runtime, second_server.async_close(), std::chrono::milliseconds{5'000}, "second server close");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_connection_cancel_rejects_new_streams) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   (void)accept_future.get();
   client_connection.cancel();

   try {
      (void)fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
      BOOST_FAIL("expected canceled connection to reject new streams");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::connection_closed || fcl::quic::exceptions::code_of(error).value() == exceptions::code::canceled;
      BOOST_TEST(acceptable);
   }
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_verifies_pinned_peer_fingerprint) {
   const auto expected = sha256_fingerprint(test_certificate_der());
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto client = connector{runtime};

   auto connection = fcl::asio::blocking::run(
       runtime, client.async_connect(
                    server.local_endpoint(),
                    client_options{
                        .handshake_timeout = std::chrono::milliseconds{5'000},
                        .security = security_options{.verify_peer = true, .expected_sha256_fingerprint = expected},
                    }));

   BOOST_TEST(connection.valid());
   fcl::asio::blocking::run(runtime, connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_verifies_ca_certificate_hostname) {
   const auto identity = generate_test_identity("DNS:localhost,IP:127.0.0.1");
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options_value = loopback_server_options();
   server_options_value.certificate_pem = identity.certificate_pem;
   server_options_value.private_key_pem = identity.private_key_pem;
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, std::move(server_options_value)};
   auto client = connector{runtime};

   auto client_options_value = loopback_client_options();
   client_options_value.security = security_options{
       .verify_peer = true,
       .trusted_ca_pem = identity.certificate_pem,
   };
   auto connection =
       run_with_deadline(runtime, client.async_connect(server.local_endpoint(), std::move(client_options_value)),
                         std::chrono::milliseconds{5'000}, "CA verified hostname connect");

   BOOST_TEST(connection.valid());
   run_with_deadline(runtime, connection.async_close(), std::chrono::milliseconds{5'000}, "CA verified hostname close");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_ca_certificate_hostname_mismatch) {
   const auto identity = generate_test_identity("DNS:example.invalid,IP:127.0.0.2");
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options_value = loopback_server_options();
   server_options_value.handshake_timeout = std::chrono::milliseconds{500};
   server_options_value.certificate_pem = identity.certificate_pem;
   server_options_value.private_key_pem = identity.private_key_pem;
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, std::move(server_options_value)};
   auto client = connector{runtime};

   auto client_options_value = loopback_client_options();
   client_options_value.security = security_options{
       .verify_peer = true,
       .trusted_ca_pem = identity.certificate_pem,
   };
   try {
      (void)run_with_deadline(runtime, client.async_connect(server.local_endpoint(), std::move(client_options_value)),
                              std::chrono::milliseconds{5'000}, "CA hostname mismatch connect");
      BOOST_FAIL("expected QUIC hostname verification failure");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::tls_failed ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::peer_verification_failed ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::handshake_timeout || fcl::quic::exceptions::code_of(error).value() == exceptions::code::canceled;
      BOOST_TEST_CONTEXT("error kind=" << static_cast<int>(fcl::quic::exceptions::code_of(error).value()) << " message=" << error.what()) {
         BOOST_TEST(acceptable);
      }
   }
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_accepts_mtls_client_certificate) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options_value = loopback_server_options();
   server_options_value.security = security_options{
       .verify_peer = true,
       .expected_sha256_fingerprint = sha256_fingerprint(test_certificate_der()),
   };
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, std::move(server_options_value)};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);

   auto client_options_value = loopback_client_options();
   client_options_value.certificate_pem = std::string{test_certificate()};
   client_options_value.private_key_pem = std::string{test_private_key()};
   auto client = connector{runtime};
   auto connection =
       run_with_deadline(runtime, client.async_connect(server.local_endpoint(), std::move(client_options_value)),
                         std::chrono::milliseconds{5'000}, "mTLS client connect");
   auto accepted = get_with_deadline(accept_future, std::chrono::milliseconds{5'000}, "mTLS server accept");

   BOOST_TEST(connection.valid());
   BOOST_TEST(accepted.peer_certificate().has_value());
   run_with_deadline(runtime, connection.async_close(), std::chrono::milliseconds{5'000}, "mTLS client close");
   run_with_deadline(runtime, accepted.async_close(), std::chrono::milliseconds{5'000}, "mTLS server close");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_missing_mtls_client_certificate) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options_value = loopback_server_options();
   server_options_value.handshake_timeout = std::chrono::milliseconds{500};
   server_options_value.security = security_options{.verify_peer = true};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, std::move(server_options_value)};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};

   auto client_connected = false;
   try {
      auto connection =
          run_with_deadline(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()),
                            std::chrono::milliseconds{5'000}, "missing mTLS client cert connect");
      client_connected = connection.valid();
      run_with_deadline(runtime, connection.async_close(), std::chrono::milliseconds{5'000},
                        "close missing-cert client");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::peer_verification_failed ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::tls_failed || fcl::quic::exceptions::code_of(error).value() == exceptions::code::handshake_timeout;
      BOOST_TEST(acceptable);
   }
   try {
      (void)get_with_deadline(accept_future, std::chrono::milliseconds{5'000}, "missing-cert server accept");
      BOOST_FAIL("expected missing client certificate to reject server accept");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::peer_verification_failed ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::tls_failed || fcl::quic::exceptions::code_of(error).value() == exceptions::code::handshake_timeout ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::connection_closed;
      BOOST_TEST(acceptable);
   }
   (void)client_connected;
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_wrong_peer_fingerprint) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto client = connector{runtime};

   try {
      (void)fcl::asio::blocking::run(
          runtime,
          client.async_connect(server.local_endpoint(),
                               client_options{
                                   .handshake_timeout = std::chrono::milliseconds{5'000},
                                   .security =
                                       security_options{
                                           .verify_peer = true,
                                           .expected_sha256_fingerprint =
                                               "0000000000000000000000000000000000000000000000000000000000000000",
                                       },
                               }));
      BOOST_FAIL("expected peer fingerprint rejection");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::peer_verification_failed));
   }
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_connection_close_unblocks_pending_stream_read) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   auto server_connection = accept_future.get();
   auto stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());

   auto read_future = boost::asio::co_spawn(runtime.context(), stream.async_read(), boost::asio::use_future);
   run_with_deadline(runtime, client_connection.async_close(), std::chrono::milliseconds{5'000},
                     "close while stream read is pending");

   try {
      (void)get_with_deadline(read_future, std::chrono::milliseconds{5'000}, "pending stream read after close");
      BOOST_FAIL("expected pending stream read to unblock with a close error");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::connection_closed ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::stream_closed || fcl::quic::exceptions::code_of(error).value() == exceptions::code::stream_reset;
      BOOST_TEST(acceptable);
   }
   run_with_deadline(runtime, server_connection.async_close(), std::chrono::milliseconds{5'000},
                     "server close after pending read");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_max_streams_backpressure) {
   auto limits =
       transport_limits{.max_connections = 16, .max_streams_per_connection = 1, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto client = connector{runtime};
   auto connection = fcl::asio::blocking::run(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", limits)));

   auto first = fcl::asio::blocking::run(runtime, connection.async_open_stream());
   BOOST_TEST(first.valid());
   try {
      (void)fcl::asio::blocking::run(runtime, connection.async_open_stream());
      BOOST_FAIL("expected max streams rejection");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::backpressure_rejected));
   }
   fcl::asio::blocking::run(runtime, connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_allows_new_stream_after_previous_stream_closes) {
   auto client_limits =
       transport_limits{.max_connections = 16, .max_streams_per_connection = 1, .max_queued_bytes = 16 * 1024 * 1024};
   auto server_limits =
       transport_limits{.max_connections = 16, .max_streams_per_connection = 16, .max_queued_bytes = 16 * 1024 * 1024};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", server_limits)};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto connection = run_with_deadline(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", client_limits)),
       std::chrono::milliseconds{5'000}, "stream reuse connect");
   auto server_connection = std::make_shared<fcl::quic::connection>(
       get_with_deadline(accept_future, std::chrono::milliseconds{5'000}, "stream reuse accept"));

   for (auto index = 0U; index < 2U; ++index) {
      auto server_task = boost::asio::co_spawn(
          runtime.context(),
          [server_connection]() -> boost::asio::awaitable<void> {
             auto accepted = co_await server_connection->async_accept_stream();
             auto framed = framed_stream{std::move(accepted)};
             (void)co_await framed.async_read_frame();
             co_await framed.async_close();
          },
          boost::asio::use_future);

      auto stream = run_with_deadline(runtime, connection.async_open_stream(), std::chrono::milliseconds{5'000},
                                      "open active-limited stream");
      auto framed = framed_stream{std::move(stream)};
      run_with_deadline(runtime, framed.async_write_frame(std::vector<std::uint8_t>{static_cast<std::uint8_t>(index)}),
                        std::chrono::milliseconds{5'000}, "write active-limited stream");
      run_with_deadline(runtime, framed.async_close(), std::chrono::milliseconds{5'000}, "close active-limited stream");
      try {
         (void)run_with_deadline(runtime, framed.async_read_frame(), std::chrono::milliseconds{5'000},
                                 "observe active-limited stream close");
      } catch (const fcl::exception::base& error) {
         const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::stream_closed ||
                                 fcl::quic::exceptions::code_of(error).value() == exceptions::code::connection_closed ||
                                 fcl::quic::exceptions::code_of(error).value() == exceptions::code::stream_reset;
         BOOST_TEST(acceptable);
      }
      get_with_deadline(server_task, std::chrono::milliseconds{5'000}, "stream reuse server task");
   }

   run_with_deadline(runtime, connection.async_close(), std::chrono::milliseconds{5'000},
                     "stream reuse connection close");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_max_queued_bytes_backpressure) {
   auto limits = transport_limits{.max_connections = 16, .max_streams_per_connection = 16, .max_queued_bytes = 3};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", limits)};
   auto client = connector{runtime};
   auto connection = fcl::asio::blocking::run(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", limits)));
   auto outbound = fcl::asio::blocking::run(runtime, connection.async_open_stream());

   try {
      const auto payload = std::vector<std::uint8_t>{1, 2, 3, 4};
      fcl::asio::blocking::run(runtime, outbound.async_write(payload));
      BOOST_FAIL("expected queued bytes rejection");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::backpressure_rejected));
   }
   BOOST_TEST(connection.metrics().backpressure_rejections >= 1U);
   fcl::asio::blocking::run(runtime, connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_loopback_rejects_inbound_packet_queue_overflow) {
   auto server_limits = transport_limits{
       .max_connections = 16,
       .max_streams_per_connection = 16,
       .max_queued_bytes = 16 * 1024 * 1024,
   };
   auto client_limits = transport_limits{
       .max_connections = 16,
       .max_streams_per_connection = 16,
       .max_queued_bytes = 16 * 1024 * 1024,
       .max_inbound_queued_bytes = 1,
       .max_inbound_queued_packets = 16,
   };
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server =
       listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options("fcl-p2p/1", server_limits)};
   auto client = connector{runtime};

   try {
      (void)run_with_deadline(
          runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", client_limits)),
          std::chrono::milliseconds{5'000}, "inbound overflow connect");
      BOOST_FAIL("expected inbound packet queue overflow to close the connection");
   } catch (const fcl::exception::base& error) {
      const auto acceptable = fcl::quic::exceptions::code_of(error).value() == exceptions::code::connection_closed || fcl::quic::exceptions::code_of(error).value() == exceptions::code::canceled ||
                              fcl::quic::exceptions::code_of(error).value() == exceptions::code::backpressure_rejected;
      BOOST_TEST(acceptable);
   }

   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto valid = run_with_deadline(
       runtime, client.async_connect(server.local_endpoint(), loopback_client_options("fcl-p2p/1", server_limits)),
       std::chrono::milliseconds{5'000}, "valid connect after inbound overflow");
   auto accepted = get_with_deadline(accept_future, std::chrono::milliseconds{5'000}, "accept after inbound overflow");
   BOOST_TEST(valid.valid());
   BOOST_TEST(accepted.valid());
   BOOST_TEST(!accepted.metrics().closed);
   run_with_deadline(runtime, valid.async_close(), std::chrono::milliseconds{5'000},
                     "valid close after inbound overflow");
   run_with_deadline(runtime, accepted.async_close(), std::chrono::milliseconds{5'000},
                     "accepted close after inbound overflow");
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_framed_stream_rejects_oversized_remote_frame) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);
   auto client = connector{runtime};
   auto client_connection =
       fcl::asio::blocking::run(runtime, client.async_connect(server.local_endpoint(), loopback_client_options()));
   auto server_connection = accept_future.get();

   auto server_send = boost::asio::co_spawn(
       runtime.context(),
       [server_connection = std::move(server_connection)]() mutable -> boost::asio::awaitable<void> {
          auto accepted = co_await server_connection.async_accept_stream();
          auto framed = framed_stream{std::move(accepted)};
          (void)co_await framed.async_read_frame();
          const auto payload = std::vector<std::uint8_t>{1, 2, 3, 4};
          co_await framed.async_write_frame(payload);
       },
       boost::asio::use_future);

   auto client_stream = fcl::asio::blocking::run(runtime, client_connection.async_open_stream());
   auto framed = framed_stream{std::move(client_stream), frame_codec_options{.max_frame_size = 3}};
   fcl::asio::blocking::run(runtime, framed.async_write_frame(std::vector<std::uint8_t>{'g', 'o'}));
   try {
      (void)fcl::asio::blocking::run(runtime, framed.async_read_frame());
      BOOST_FAIL("expected oversized frame rejection");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::frame_too_large));
   }
   server_send.get();
   fcl::asio::blocking::run(runtime, client_connection.async_close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(quic_listener_stop_unblocks_pending_accept) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto server = listener{runtime, endpoint{.host = "127.0.0.1", .port = 0}, loopback_server_options()};
   auto accept_future = boost::asio::co_spawn(runtime.context(), server.async_accept(), boost::asio::use_future);

   server.stop();

   try {
      (void)accept_future.get();
      BOOST_FAIL("expected stopped listener to unblock accept with an error");
   } catch (const fcl::exception::base& error) {
      BOOST_TEST(static_cast<int>(fcl::quic::exceptions::code_of(error).value()) == static_cast<int>(exceptions::code::connection_closed));
   }
}

} // namespace
} // namespace fcl::quic
