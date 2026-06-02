#include "quic_engine.hpp"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

import fcl.crypto.random;
import fcl.crypto.sha256;

namespace fcl::quic::detail {
namespace {

namespace asio = boost::asio;
using udp = asio::ip::udp;

constexpr auto cid_length = std::size_t{8};
constexpr auto max_udp_payload_size = std::size_t{1350};
constexpr auto max_packets_per_drain = std::size_t{64};
constexpr auto max_queued_datagram_bytes = std::size_t{16 * 1024 * 1024};
constexpr auto stateless_reset_secret_size = std::size_t{32};

using timer_ptr = std::shared_ptr<asio::steady_timer>;
using stateless_reset_secret = std::array<std::uint8_t, stateless_reset_secret_size>;

[[nodiscard]] ngtcp2_tstamp timestamp() noexcept {
   return static_cast<ngtcp2_tstamp>(
       std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
           .count());
}

[[nodiscard]] std::string openssl_error() {
   auto out = std::string{};
   auto code = 0UL;
   while ((code = ERR_get_error()) != 0) {
      if (!out.empty()) {
         out += "; ";
      }
      out += ERR_error_string(code, nullptr);
   }
   return out.empty() ? "OpenSSL operation failed" : out;
}

[[noreturn]] void throw_engine(engine_error_kind kind, std::string message) {
   throw engine_failure{kind, std::move(message)};
}

[[nodiscard]] std::string ngtcp2_read_error_message(ngtcp2_conn* conn, int rv) {
   auto out = std::string{"ngtcp2_conn_read_pkt failed: "};
   out += ngtcp2_strerror(rv);
   if (conn == nullptr) {
      return out;
   }
   out += "; client_version=0x";
   {
      auto stream = std::ostringstream{};
      stream << std::hex << ngtcp2_conn_get_client_chosen_version(conn);
      out += stream.str();
   }
   out += "; negotiated_version=0x";
   {
      auto stream = std::ostringstream{};
      stream << std::hex << ngtcp2_conn_get_negotiated_version(conn);
      out += stream.str();
   }
   if (const auto tls_error = ngtcp2_conn_get_tls_error(conn); tls_error != 0) {
      out += "; tls_error=";
      out += std::to_string(tls_error);
      out += " ";
      out += ERR_error_string(static_cast<unsigned long>(tls_error), nullptr);
   }
   if (const auto alert = ngtcp2_conn_get_tls_alert(conn); alert != 0) {
      out += "; tls_alert=";
      out += SSL_alert_desc_string_long(alert);
   }
   const auto* close_error = ngtcp2_conn_get_ccerr(conn);
   if (close_error != nullptr && (close_error->type != NGTCP2_CCERR_TYPE_IDLE_CLOSE || close_error->error_code != 0 ||
                                  close_error->reasonlen != 0)) {
      out += "; close_type=";
      out += std::to_string(static_cast<int>(close_error->type));
      out += "; close_code=";
      out += std::to_string(close_error->error_code);
      if (close_error->frame_type != 0) {
         out += "; close_frame=";
         out += std::to_string(close_error->frame_type);
      }
      if (close_error->reason != nullptr && close_error->reasonlen != 0) {
         out += "; close_reason=";
         out.append(reinterpret_cast<const char*>(close_error->reason), close_error->reasonlen);
      }
   }
   return out;
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout_budget(std::chrono::steady_clock::time_point started,
                                                                 std::chrono::milliseconds timeout) noexcept {
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   if (elapsed >= timeout) {
      return std::chrono::milliseconds{0};
   }
   return timeout - elapsed;
}

[[nodiscard]] bool connect_failpoint_enabled(std::string_view name) {
   const auto* value = std::getenv("STORLANE_NETWORK_QUIC_CONNECT_FAILPOINT");
   return value != nullptr && std::string_view{value} == name;
}

int accept_any_certificate_cb(int, X509_STORE_CTX*) {
   return 1;
}

[[nodiscard]] bool fill_random(std::span<std::uint8_t> bytes) {
   try {
      fcl::crypto::fill_random(bytes);
      return true;
   } catch (...) {
      return false;
   }
}

[[nodiscard]] stateless_reset_secret random_stateless_reset_secret() {
   auto secret = stateless_reset_secret{};
   if (!fill_random(secret)) {
      throw_engine(engine_error_kind::tls_failed, "failed to generate QUIC stateless reset secret");
   }
   return secret;
}

void rand_cb(std::uint8_t* dest, std::size_t destlen, const ngtcp2_rand_ctx*) {
   (void)fill_random({dest, destlen});
}

int get_new_connection_id_cb(ngtcp2_conn*, ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, std::size_t cidlen,
                             void* user_data);
int remove_connection_id_cb(ngtcp2_conn*, const ngtcp2_cid* cid, void* user_data);

[[nodiscard]] std::string cid_key(const ngtcp2_cid& cid) {
   return std::string{reinterpret_cast<const char*>(cid.data), reinterpret_cast<const char*>(cid.data) + cid.datalen};
}

[[nodiscard]] std::string cid_key(const std::uint8_t* data, std::size_t len) {
   return std::string{reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + len};
}

[[nodiscard]] sockaddr_storage to_sockaddr_storage(const udp::endpoint& endpoint) {
   auto storage = sockaddr_storage{};
   if (endpoint.address().is_v4()) {
      auto addr = sockaddr_in{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(endpoint.port());
      const auto bytes = endpoint.address().to_v4().to_bytes();
      static_assert(sizeof(addr.sin_addr.s_addr) == bytes.size());
      std::memcpy(&addr.sin_addr.s_addr, bytes.data(), bytes.size());
      std::memcpy(&storage, &addr, sizeof(addr));
   } else {
      auto addr = sockaddr_in6{};
      addr.sin6_family = AF_INET6;
      addr.sin6_port = htons(endpoint.port());
      const auto bytes = endpoint.address().to_v6().to_bytes();
      static_assert(sizeof(addr.sin6_addr.s6_addr) == bytes.size());
      std::memcpy(addr.sin6_addr.s6_addr, bytes.data(), bytes.size());
      std::memcpy(&storage, &addr, sizeof(addr));
   }
   return storage;
}

[[nodiscard]] ngtcp2_addr to_ngtcp2_addr(sockaddr_storage& storage) {
   auto* addr = reinterpret_cast<sockaddr*>(&storage);
   const auto len = addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
   return ngtcp2_addr{.addr = addr, .addrlen = static_cast<socklen_t>(len)};
}

struct path_storage {
   sockaddr_storage local_storage{};
   sockaddr_storage remote_storage{};
   ngtcp2_path path{};
};

[[nodiscard]] path_storage make_path(const udp::endpoint& local, const udp::endpoint& remote) {
   auto storage = path_storage{};
   storage.local_storage = to_sockaddr_storage(local);
   storage.remote_storage = to_sockaddr_storage(remote);
   storage.path.local = to_ngtcp2_addr(storage.local_storage);
   storage.path.remote = to_ngtcp2_addr(storage.remote_storage);
   return storage;
}

[[nodiscard]] std::vector<std::uint8_t> length_prefixed_alpn(std::string_view alpn) {
   auto out = std::vector<std::uint8_t>{};
   if (alpn.empty() || alpn.size() > 255) {
      throw_engine(engine_error_kind::invalid_options, "QUIC ALPN must be 1..255 bytes");
   }
   out.push_back(static_cast<std::uint8_t>(alpn.size()));
   out.insert(out.end(), alpn.begin(), alpn.end());
   return out;
}

int select_alpn_cb(SSL*, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen,
                   void* arg) {
   const auto& alpn = *static_cast<const std::string*>(arg);
   auto pos = std::size_t{0};
   while (pos < inlen) {
      const auto len = static_cast<std::size_t>(in[pos]);
      ++pos;
      if (pos + len > inlen) {
         return SSL_TLSEXT_ERR_ALERT_FATAL;
      }
      if (len == alpn.size() && std::memcmp(in + pos, alpn.data(), len) == 0) {
         *out = in + pos;
         *outlen = static_cast<unsigned char>(len);
         return SSL_TLSEXT_ERR_OK;
      }
      pos += len;
   }
   return SSL_TLSEXT_ERR_ALERT_FATAL;
}

struct ssl_ctx_deleter {
   void operator()(SSL_CTX* ctx) const noexcept {
      SSL_CTX_free(ctx);
   }
};

struct ssl_deleter {
   void operator()(SSL* ssl) const noexcept {
      if (ssl != nullptr) {
         SSL_set_app_data(ssl, nullptr);
      }
      SSL_free(ssl);
   }
};

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

using ssl_ctx_ptr = std::unique_ptr<SSL_CTX, ssl_ctx_deleter>;
using ssl_ptr = std::unique_ptr<SSL, ssl_deleter>;
using x509_ptr = std::unique_ptr<X509, x509_deleter>;
using pkey_ptr = std::unique_ptr<EVP_PKEY, pkey_deleter>;

[[nodiscard]] x509_ptr load_certificate(std::string_view pem) {
   auto* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
   if (bio == nullptr) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   auto* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
   BIO_free(bio);
   if (cert == nullptr) {
      throw_engine(engine_error_kind::tls_failed, "invalid QUIC server certificate: " + openssl_error());
   }
   return x509_ptr{cert};
}

[[nodiscard]] pkey_ptr load_private_key(std::string_view pem) {
   auto* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
   if (bio == nullptr) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   auto* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
   BIO_free(bio);
   if (key == nullptr) {
      throw_engine(engine_error_kind::tls_failed, "invalid QUIC server private key: " + openssl_error());
   }
   return pkey_ptr{key};
}

void add_trusted_certificate(SSL_CTX* ctx, std::string_view pem) {
   auto* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
   if (bio == nullptr) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   auto bio_guard = std::unique_ptr<BIO, decltype(&BIO_free)>{bio, BIO_free};
   auto* store = SSL_CTX_get_cert_store(ctx);
   if (store == nullptr) {
      throw_engine(engine_error_kind::tls_failed, "failed to access QUIC TLS trust store");
   }

   auto loaded = false;
   while (auto* raw = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) {
      loaded = true;
      auto certificate = x509_ptr{raw};
      if (X509_STORE_add_cert(store, certificate.get()) != 1) {
         const auto code = ERR_peek_last_error();
         if (ERR_GET_REASON(code) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
            ERR_clear_error();
            continue;
         }
         throw_engine(engine_error_kind::tls_failed, "failed to add trusted QUIC CA certificate: " + openssl_error());
      }
   }

   const auto code = ERR_peek_last_error();
   if (code != 0 && ERR_GET_REASON(code) != PEM_R_NO_START_LINE) {
      throw_engine(engine_error_kind::tls_failed, "failed to parse trusted QUIC CA certificate: " + openssl_error());
   }
   ERR_clear_error();
   if (!loaded) {
      throw_engine(engine_error_kind::tls_failed, "trusted QUIC CA PEM does not contain a certificate");
   }
}

void configure_default_trust(SSL_CTX* ctx, const engine_security_options& security) {
   if (!security.trusted_ca_pem.empty()) {
      add_trusted_certificate(ctx, security.trusted_ca_pem);
      return;
   }
   if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
      throw_engine(engine_error_kind::tls_failed, "failed to load default QUIC TLS trust paths: " + openssl_error());
   }
}

[[nodiscard]] bool configure_verify_peer_name(SSL* ssl, std::string_view host) {
   auto* params = SSL_get0_param(ssl);
   if (params == nullptr) {
      throw_engine(engine_error_kind::tls_failed, "failed to access QUIC TLS verification parameters");
   }

   const auto peer_name = host.empty() ? std::string{"localhost"} : std::string{host};
   if (X509_VERIFY_PARAM_set1_ip_asc(params, peer_name.c_str()) == 1) {
      return false;
   }
   ERR_clear_error();
   if (SSL_set1_host(ssl, peer_name.c_str()) != 1) {
      throw_engine(engine_error_kind::tls_failed, "failed to bind QUIC TLS peer hostname: " + openssl_error());
   }
   return true;
}

[[nodiscard]] std::vector<std::uint8_t> der_from_certificate(X509* certificate) {
   if (certificate == nullptr) {
      return {};
   }
   const auto len = i2d_X509(certificate, nullptr);
   if (len <= 0) {
      throw_engine(engine_error_kind::tls_failed, "failed to DER-encode peer certificate");
   }
   auto der = std::vector<std::uint8_t>(static_cast<std::size_t>(len));
   auto* out = der.data();
   if (i2d_X509(certificate, &out) != len) {
      throw_engine(engine_error_kind::tls_failed, "failed to DER-encode peer certificate");
   }
   return der;
}

void wake(std::vector<std::weak_ptr<asio::steady_timer>>& waiters) {
   auto current = std::move(waiters);
   waiters.clear();
   for (auto& weak : current) {
      if (auto timer = weak.lock()) {
         timer->cancel();
      }
   }
}

[[nodiscard]] engine_endpoint from_udp_endpoint(const udp::endpoint& value) {
   return engine_endpoint{.host = value.address().to_string(), .port = value.port()};
}

} // namespace

engine_failure::engine_failure(engine_error_kind kind, std::string message)
    : kind_(kind), message_(std::move(message)) {}

engine_error_kind engine_failure::kind() const noexcept {
   return kind_;
}

const char* engine_failure::what() const noexcept {
   return message_.c_str();
}

const std::string& engine_failure::message() const noexcept {
   return message_;
}

std::string normalize_engine_sha256_fingerprint(std::string_view value) {
   auto normalized = std::string{};
   normalized.reserve(value.size());
   for (const auto ch : value) {
      if (ch == ':' || ch == '-' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
         continue;
      }
      if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) {
         throw_engine(engine_error_kind::invalid_options, "invalid SHA-256 fingerprint");
      }
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
   }
   if (normalized.size() != 64) {
      throw_engine(engine_error_kind::invalid_options, "SHA-256 fingerprint must contain 32 bytes");
   }
   return normalized;
}

std::string engine_sha256_fingerprint(std::span<const std::uint8_t> data) {
   const auto digest = fcl::crypto::sha256::hash(data).to_uint8_span();

   auto out = std::ostringstream{};
   out << std::hex << std::setfill('0');
   for (const auto byte : digest) {
      out << std::setw(2) << static_cast<unsigned>(byte);
   }
   return out.str();
}

struct engine_stream::impl {
   struct pending_write {
      std::vector<std::uint8_t> data;
      std::size_t submitted = 0;
      std::uint64_t base_offset = 0;
      bool base_offset_set = false;
      bool fin = false;
      std::vector<std::weak_ptr<asio::steady_timer>> waiters;
   };

   struct retained_write {
      std::vector<std::uint8_t> data;
      std::uint64_t base_offset = 0;
      bool fin = false;
   };

   explicit impl(std::int64_t id_value) : id(id_value) {}

   std::int64_t id = -1;
   std::weak_ptr<engine_connection::impl> connection;
   std::map<std::uint64_t, std::vector<std::uint8_t>> inbound_segments;
   std::deque<std::vector<std::uint8_t>> inbound_ready;
   std::deque<pending_write> outbound;
   std::deque<retained_write> retained;
   std::uint64_t recv_next_offset = 0;
   std::uint64_t send_next_offset = 0;
   bool remote_read_closed = false;
   bool local_write_closed = false;
   bool reset = false;
   bool closed = false;
   std::vector<std::weak_ptr<asio::steady_timer>> read_waiters;
   std::vector<std::weak_ptr<asio::steady_timer>> write_waiters;
};

struct engine_connection_metrics_state {
   std::atomic<std::uint64_t> connections_opened{0};
   std::atomic<std::uint64_t> connections_closed{0};
   std::atomic<std::uint64_t> handshakes_started{0};
   std::atomic<std::uint64_t> handshakes_completed{0};
   std::atomic<std::uint64_t> handshakes_failed{0};
   std::atomic<std::uint64_t> streams_opened{0};
   std::atomic<std::uint64_t> streams_accepted{0};
   std::atomic<std::uint64_t> streams_reset{0};
   std::atomic<std::uint64_t> frames_sent{0};
   std::atomic<std::uint64_t> frames_received{0};
   std::atomic<std::uint64_t> bytes_sent{0};
   std::atomic<std::uint64_t> bytes_received{0};
   std::atomic<std::uint64_t> packets_sent{0};
   std::atomic<std::uint64_t> packets_received{0};
   std::atomic<std::uint64_t> timeouts{0};
   std::atomic<std::uint64_t> cancellations{0};
   std::atomic<std::uint64_t> backpressure_rejections{0};
   std::atomic<std::size_t> queued_bytes{0};
   std::atomic<std::size_t> active_streams{0};
   std::atomic<bool> closed{false};

   [[nodiscard]] engine_connection_metrics snapshot() const noexcept {
      const auto relaxed = std::memory_order_relaxed;
      return engine_connection_metrics{
          .connections_opened = connections_opened.load(relaxed),
          .connections_closed = connections_closed.load(relaxed),
          .handshakes_started = handshakes_started.load(relaxed),
          .handshakes_completed = handshakes_completed.load(relaxed),
          .handshakes_failed = handshakes_failed.load(relaxed),
          .streams_opened = streams_opened.load(relaxed),
          .streams_accepted = streams_accepted.load(relaxed),
          .streams_reset = streams_reset.load(relaxed),
          .frames_sent = frames_sent.load(relaxed),
          .frames_received = frames_received.load(relaxed),
          .bytes_sent = bytes_sent.load(relaxed),
          .bytes_received = bytes_received.load(relaxed),
          .packets_sent = packets_sent.load(relaxed),
          .packets_received = packets_received.load(relaxed),
          .timeouts = timeouts.load(relaxed),
          .cancellations = cancellations.load(relaxed),
          .backpressure_rejections = backpressure_rejections.load(relaxed),
          .queued_bytes = queued_bytes.load(relaxed),
          .active_streams = active_streams.load(relaxed),
          .closed = closed.load(relaxed),
      };
   }
};

struct engine_connection::impl {
   struct queued_packet {
      std::vector<std::uint8_t> bytes;
      udp::endpoint from;
   };

   impl(asio::io_context& context_value, std::shared_ptr<udp::socket> socket_value, udp::endpoint remote_endpoint_value,
        engine_transport_limits limits_value)
       : context(context_value), strand(asio::make_strand(context_value)), socket(std::move(socket_value)),
         remote_endpoint(std::move(remote_endpoint_value)), limits(limits_value), expiry_timer(strand) {}

   ~impl() {
      if (conn != nullptr) {
         ngtcp2_conn_del(conn);
      }
      if (ossl_ctx != nullptr) {
         ngtcp2_crypto_ossl_ctx_del(ossl_ctx);
      }
      if (ssl) {
         SSL_set_app_data(ssl.get(), nullptr);
      }
   }

   asio::io_context& context;
   asio::strand<asio::io_context::executor_type> strand;
   std::shared_ptr<udp::socket> socket;
   udp::endpoint remote_endpoint;
   engine_transport_limits limits;
   engine_connection_metrics_state metrics{};
   stateless_reset_secret reset_secret = random_stateless_reset_secret();
   std::weak_ptr<impl> self;

   ngtcp2_conn* conn = nullptr;
   ngtcp2_crypto_ossl_ctx* ossl_ctx = nullptr;
   ngtcp2_crypto_conn_ref conn_ref{};
   ssl_ctx_ptr ssl_ctx;
   ssl_ptr ssl;
   engine_security_options peer_security{};

   std::unordered_map<std::int64_t, std::shared_ptr<engine_stream::impl>> streams;
   std::deque<std::shared_ptr<engine_stream::impl>> accepted_streams;
   std::vector<std::weak_ptr<asio::steady_timer>> handshake_waiters;
   std::vector<std::weak_ptr<asio::steady_timer>> accept_stream_waiters;
   std::function<void()> handshake_completed_hook;
   std::function<void(std::shared_ptr<impl>)> closed_hook;
   std::function<void(const ngtcp2_cid&)> local_connection_id_issued_hook;
   std::function<void(const ngtcp2_cid&)> local_connection_id_retired_hook;

   asio::steady_timer expiry_timer;
   std::deque<std::vector<std::uint8_t>> outbound_datagrams;
   std::deque<queued_packet> inbound_packets;
   std::size_t queued_datagram_bytes = 0;
   std::size_t queued_inbound_packet_bytes = 0;
   bool handshake_done = false;
   bool closing = false;
   bool canceled = false;
   bool closed_hook_called = false;
   bool receive_loop_started = false;
   bool drain_active = false;
   bool drain_requested = false;
   bool udp_send_active = false;
   bool packet_processing_active = false;
   bool expiry_event_pending = false;
   bool server_side = false;
   bool listener_accept_notified = false;

   [[nodiscard]] udp::endpoint local_endpoint() const {
      boost::system::error_code ec;
      const auto local = socket->local_endpoint(ec);
      if (ec) {
         return udp::endpoint{remote_endpoint.protocol(), 0};
      }
      return local;
   }

   [[nodiscard]] std::shared_ptr<engine_stream::impl> ensure_stream(std::int64_t stream_id) {
      if (auto it = streams.find(stream_id); it != streams.end()) {
         return it->second;
      }
      auto stream = std::make_shared<engine_stream::impl>(stream_id);
      stream->connection = self;
      streams.emplace(stream_id, stream);
      update_active_stream_metrics();
      if (conn != nullptr) {
         (void)ngtcp2_conn_set_stream_user_data(conn, stream_id, stream.get());
      }
      return stream;
   }

   [[nodiscard]] static bool stream_is_active(const std::shared_ptr<engine_stream::impl>& stream) noexcept {
      return stream && !stream->closed && !stream->reset && !(stream->local_write_closed && stream->remote_read_closed);
   }

   [[nodiscard]] std::size_t active_stream_count() const {
      return static_cast<std::size_t>(
          std::ranges::count_if(streams, [](const auto& item) { return stream_is_active(item.second); }));
   }

   void update_active_stream_metrics() {
      metrics.active_streams.store(active_stream_count(), std::memory_order_relaxed);
   }

   void clear_queued_work() {
      outbound_datagrams.clear();
      inbound_packets.clear();
      queued_datagram_bytes = 0;
      queued_inbound_packet_bytes = 0;
      metrics.queued_bytes.store(0, std::memory_order_relaxed);
   }

   void wake_and_clear_streams(bool reset_streams) {
      for (auto& [_, stream] : streams) {
         if (reset_streams) {
            stream->reset = true;
         } else {
            stream->closed = true;
         }
         wake(stream->read_waiters);
         wake(stream->write_waiters);
         for (auto& write : stream->outbound) {
            wake(write.waiters);
         }
         stream->outbound.clear();
         stream->retained.clear();
      }
      update_active_stream_metrics();
   }

   void notify_closed_once() {
      if (closed_hook_called) {
         return;
      }
      closed_hook_called = true;
      if (closed_hook) {
         if (auto shared = self.lock()) {
            closed_hook(std::move(shared));
         }
      }
   }

   void cancel_transport_io(bool close_socket) {
      boost::system::error_code ignored;
      if (close_socket && socket) {
         socket->cancel(ignored);
         socket->close(ignored);
      }
      expiry_timer.cancel();
   }

   void fail_all() {
      canceled = true;
      closing = true;
      metrics.closed.store(true, std::memory_order_relaxed);
      clear_queued_work();
      cancel_transport_io(!server_side);
      wake(handshake_waiters);
      wake(accept_stream_waiters);
      wake_and_clear_streams(true);
      notify_closed_once();
   }

   void close_transport(bool cancel_socket) {
      closing = true;
      metrics.closed.store(true, std::memory_order_relaxed);
      clear_queued_work();
      cancel_transport_io(cancel_socket);
      wake(handshake_waiters);
      wake(accept_stream_waiters);
      wake_and_clear_streams(false);
      notify_closed_once();
   }

   void verify_selected_alpn(std::string_view expected) {
      const unsigned char* selected = nullptr;
      unsigned int selected_len = 0;
      SSL_get0_alpn_selected(ssl.get(), &selected, &selected_len);
      if (selected_len != expected.size() || selected == nullptr ||
          std::memcmp(selected, expected.data(), selected_len) != 0) {
         throw_engine(engine_error_kind::alpn_mismatch, "QUIC ALPN mismatch");
      }
   }

   void verify_peer(const engine_security_options& security) {
      if (!security.verify_peer) {
         return;
      }
      if (server_side && SSL_get_peer_cert_chain(ssl.get()) == nullptr) {
         throw_engine(engine_error_kind::peer_verification_failed, "QUIC peer did not present a certificate");
      }
      auto cert = x509_ptr{SSL_get1_peer_certificate(ssl.get())};
      if (!cert) {
         throw_engine(engine_error_kind::peer_verification_failed, "QUIC peer did not present a certificate");
      }
      auto der = der_from_certificate(cert.get());
      auto peer = engine_peer_certificate{.der = std::move(der)};
      peer.sha256_fingerprint = engine_sha256_fingerprint(peer.der);
      if (security.expected_sha256_fingerprint &&
          peer.sha256_fingerprint != normalize_engine_sha256_fingerprint(*security.expected_sha256_fingerprint)) {
         throw_engine(engine_error_kind::peer_verification_failed, "QUIC peer certificate fingerprint mismatch");
      }
      if (security.verifier && !security.verifier(peer)) {
         throw_engine(engine_error_kind::peer_verification_failed, "QUIC peer verifier rejected certificate");
      }
   }

   void complete_handshake() {
      if (handshake_done) {
         return;
      }
      handshake_done = true;
      metrics.handshakes_completed.fetch_add(1, std::memory_order_relaxed);
      wake(handshake_waiters);
      if (handshake_completed_hook) {
         handshake_completed_hook();
      }
   }

   boost::asio::awaitable<void> wait_handshake(std::chrono::milliseconds timeout) {
      co_await asio::dispatch(strand, asio::use_awaitable);
      if (handshake_done) {
         co_return;
      }
      auto timer = std::make_shared<asio::steady_timer>(strand);
      timer->expires_after(timeout);
      handshake_waiters.emplace_back(timer);
      boost::system::error_code ec;
      co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
      if (handshake_done) {
         co_return;
      }
      if (canceled && metrics.backpressure_rejections.load(std::memory_order_relaxed) > 0) {
         throw_engine(engine_error_kind::backpressure_rejected,
                      "QUIC handshake stopped by inbound packet backpressure");
      }
      if (canceled) {
         throw_engine(engine_error_kind::canceled, "QUIC handshake was canceled");
      }
      if (closing) {
         throw_engine(engine_error_kind::connection_closed, "QUIC connection closed before handshake completed");
      }
      metrics.handshakes_failed.fetch_add(1, std::memory_order_relaxed);
      metrics.timeouts.fetch_add(1, std::memory_order_relaxed);
      throw_engine(engine_error_kind::handshake_timeout, "QUIC handshake timed out");
   }

   void start_udp_send_loop() {
      if (udp_send_active) {
         return;
      }
      auto shared = self.lock();
      if (!shared) {
         return;
      }
      udp_send_active = true;
      asio::co_spawn(
          strand,
          [shared]() -> asio::awaitable<void> {
             while (!shared->closing && !shared->canceled) {
                if (shared->outbound_datagrams.empty()) {
                   break;
                }
                auto packet = std::move(shared->outbound_datagrams.front());
                shared->outbound_datagrams.pop_front();
                if (shared->queued_datagram_bytes >= packet.size()) {
                   shared->queued_datagram_bytes -= packet.size();
                } else {
                   shared->queued_datagram_bytes = 0;
                }

                boost::system::error_code ec;
                co_await shared->socket->async_send_to(asio::buffer(packet.data(), packet.size()),
                                                       shared->remote_endpoint,
                                                       asio::redirect_error(asio::use_awaitable, ec));
                if (ec) {
                   if (!shared->closing) {
                      shared->fail_all();
                   }
                   break;
                }
                shared->metrics.packets_sent.fetch_add(1, std::memory_order_relaxed);
                shared->metrics.bytes_sent.fetch_add(packet.size(), std::memory_order_relaxed);
             }
             shared->udp_send_active = false;
             if (!shared->outbound_datagrams.empty() && !shared->closing && !shared->canceled) {
                shared->start_udp_send_loop();
             }
          },
          asio::detached);
   }

   void enqueue_datagram(std::span<const std::uint8_t> packet) {
      if (packet.empty()) {
         return;
      }
      if (queued_datagram_bytes + packet.size() > max_queued_datagram_bytes) {
         metrics.backpressure_rejections.fetch_add(1, std::memory_order_relaxed);
         fail_all();
         throw_engine(engine_error_kind::backpressure_rejected, "QUIC UDP datagram queue exceeds limit");
      }
      outbound_datagrams.emplace_back(packet.begin(), packet.end());
      queued_datagram_bytes += packet.size();
      start_udp_send_loop();
   }

   void request_packet_processing() {
      if (packet_processing_active || drain_active || inbound_packets.empty()) {
         return;
      }
      auto shared = self.lock();
      if (!shared) {
         return;
      }
      asio::co_spawn(
          strand,
          [shared]() -> asio::awaitable<void> {
             try {
                co_await shared->process_queued_packets();
             } catch (const engine_failure&) {
                shared->fail_all();
             }
          },
          asio::detached);
   }

   void request_expiry_processing() {
      auto shared = self.lock();
      if (!shared) {
         return;
      }
      asio::co_spawn(
          strand,
          [shared]() -> asio::awaitable<void> {
             try {
                co_await shared->handle_expiry_event();
             } catch (const engine_failure&) {
                shared->fail_all();
             }
          },
          asio::detached);
   }

   void schedule_post_ngtcp2_work() {
      if (expiry_event_pending && !drain_active && !packet_processing_active) {
         request_expiry_processing();
      }
      if (!inbound_packets.empty() && !drain_active && !packet_processing_active) {
         request_packet_processing();
      }
   }

   void schedule_expiry() {
      if (conn == nullptr || closing) {
         return;
      }
      const auto expiry = ngtcp2_conn_get_expiry(conn);
      const auto now = timestamp();
      const auto delay = expiry <= now ? std::chrono::nanoseconds{1} : std::chrono::nanoseconds{expiry - now};
      expiry_timer.expires_after(delay);
      auto shared = self.lock();
      if (!shared) {
         return;
      }
      expiry_timer.async_wait([shared](boost::system::error_code ec) {
         if (ec) {
            return;
         }
         asio::co_spawn(
             shared->strand,
             [shared]() -> asio::awaitable<void> {
                try {
                   co_await shared->handle_expiry_event();
                } catch (const engine_failure&) {
                   shared->fail_all();
                }
             },
             asio::detached);
      });
   }

   boost::asio::awaitable<void> handle_expiry_event() {
      co_await asio::dispatch(strand, asio::use_awaitable);
      if (conn == nullptr || closing) {
         co_return;
      }
      if (drain_active || packet_processing_active) {
         expiry_event_pending = true;
         co_return;
      }
      expiry_event_pending = false;
      const auto rv = ngtcp2_conn_handle_expiry(conn, timestamp());
      if (rv != 0) {
         fail_all();
         co_return;
      }
      co_await drain_send();
   }

   [[nodiscard]] std::shared_ptr<engine_stream::impl> next_writable_stream() {
      for (auto& [_, stream] : streams) {
         if (!stream->outbound.empty() && !stream->local_write_closed && !stream->reset) {
            return stream;
         }
      }
      return {};
   }

   void mark_stream_data_submitted(std::shared_ptr<engine_stream::impl>& stream, ngtcp2_ssize data_len) {
      if (!stream || data_len <= 0) {
         return;
      }
      auto& write = stream->outbound.front();
      if (!write.base_offset_set) {
         write.base_offset = stream->send_next_offset;
         write.base_offset_set = true;
      }
      write.submitted += static_cast<std::size_t>(data_len);
      stream->send_next_offset += static_cast<std::uint64_t>(data_len);
      complete_submitted_writes(stream);
   }

   void complete_submitted_writes(std::shared_ptr<engine_stream::impl>& stream) {
      while (!stream->outbound.empty()) {
         auto& write = stream->outbound.front();
         if (write.submitted < write.data.size() || (!write.fin && write.data.empty())) {
            break;
         }
         if (!write.data.empty()) {
            stream->retained.push_back(engine_stream::impl::retained_write{
                .data = std::move(write.data),
                .base_offset = write.base_offset,
                .fin = write.fin,
            });
         }
         if (write.fin) {
            stream->local_write_closed = true;
         }
         wake(write.waiters);
         stream->outbound.pop_front();
      }
      wake(stream->write_waiters);
      update_active_stream_metrics();
   }

   boost::asio::awaitable<void> drain_send() {
      co_await asio::dispatch(strand, asio::use_awaitable);
      if (drain_active) {
         drain_requested = true;
         co_return;
      }

      drain_active = true;
      auto clear = std::unique_ptr<void, void (*)(void*)>{
          this, [](void* ptr) { static_cast<impl*>(ptr)->drain_active = false; }};

      do {
         drain_requested = false;
         auto packets_this_drain = std::size_t{0};
         for (;;) {
            auto packet = std::array<std::uint8_t, max_udp_payload_size>{};
            auto ps = ngtcp2_path_storage{};
            ngtcp2_path_storage_zero(&ps);
            auto pi = ngtcp2_pkt_info{};
            const auto packet_ts = timestamp();
            auto nwrite = ngtcp2_ssize{0};
            auto selected = std::shared_ptr<engine_stream::impl>{};
            auto data_len = ngtcp2_ssize{0};

            for (;;) {
               data_len = 0;
               auto flags = std::uint32_t{0};
               auto stream_id = std::int64_t{-1};
               auto datav = ngtcp2_vec{};
               auto datavcnt = std::size_t{0};
               selected = next_writable_stream();
               if (selected) {
                  auto& write = selected->outbound.front();
                  stream_id = selected->id;
                  const auto remaining = write.data.size() - write.submitted;
                  datav.base = remaining == 0 ? nullptr : write.data.data() + write.submitted;
                  datav.len = remaining;
                  datavcnt = (remaining > 0 || write.fin) ? 1 : 0;
                  if (write.fin && remaining == 0) {
                     flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
                  }
               }

               nwrite = ngtcp2_conn_writev_stream(conn, &ps.path, &pi, packet.data(), packet.size(), &data_len, flags,
                                                  stream_id, datavcnt == 0 ? nullptr : &datav, datavcnt, packet_ts);

               if (nwrite != NGTCP2_ERR_WRITE_MORE) {
                  break;
               }

               mark_stream_data_submitted(selected, data_len);
            }

            if (nwrite < 0) {
               if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED || nwrite == NGTCP2_ERR_STREAM_SHUT_WR ||
                   nwrite == NGTCP2_ERR_STREAM_NOT_FOUND) {
                  break;
               }
               fail_all();
               throw_engine(engine_error_kind::internal_error, std::string{"ngtcp2_conn_writev_stream failed: "} +
                                                                   ngtcp2_strerror(static_cast<int>(nwrite)));
            }
            if (nwrite == 0) {
               break;
            }
            if (selected && data_len >= 0) {
               if (data_len == 0 && !selected->outbound.empty()) {
                  auto& write = selected->outbound.front();
                  if (write.fin) {
                     write.submitted = write.data.size();
                  }
               }
               mark_stream_data_submitted(selected, data_len);
               complete_submitted_writes(selected);
            }
            ngtcp2_conn_update_pkt_tx_time(conn, timestamp());
            enqueue_datagram({packet.data(), static_cast<std::size_t>(nwrite)});
            ++packets_this_drain;
            if (packets_this_drain >= max_packets_per_drain) {
               drain_requested = true;
               co_await asio::post(strand, asio::use_awaitable);
               break;
            }
         }
      } while (drain_requested);

      clear.reset();
      schedule_expiry();
      schedule_post_ngtcp2_work();
   }

   boost::asio::awaitable<void> handle_packet(std::vector<std::uint8_t> packet, udp::endpoint from) {
      co_await asio::dispatch(strand, asio::use_awaitable);
      if (closing || canceled) {
         co_return;
      }
      const auto packet_size = packet.size();
      if (inbound_packets.size() >= limits.max_inbound_queued_packets ||
          packet_size > limits.max_inbound_queued_bytes ||
          queued_inbound_packet_bytes > limits.max_inbound_queued_bytes - packet_size) {
         metrics.backpressure_rejections.fetch_add(1, std::memory_order_relaxed);
         fail_all();
         throw_engine(engine_error_kind::backpressure_rejected, "QUIC inbound packet queue exceeds limit");
      }
      queued_inbound_packet_bytes += packet_size;
      inbound_packets.push_back(queued_packet{.bytes = std::move(packet), .from = std::move(from)});
      if (!drain_active && !packet_processing_active) {
         co_await process_queued_packets();
      }
   }

   boost::asio::awaitable<void> process_queued_packets() {
      co_await asio::dispatch(strand, asio::use_awaitable);
      if (packet_processing_active || drain_active) {
         co_return;
      }
      packet_processing_active = true;
      auto clear = std::unique_ptr<void, void (*)(void*)>{
          this, [](void* ptr) { static_cast<impl*>(ptr)->packet_processing_active = false; }};
      while (!inbound_packets.empty() && !closing && !canceled) {
         auto queued = std::move(inbound_packets.front());
         inbound_packets.pop_front();
         if (queued_inbound_packet_bytes >= queued.bytes.size()) {
            queued_inbound_packet_bytes -= queued.bytes.size();
         } else {
            queued_inbound_packet_bytes = 0;
         }
         auto path = make_path(local_endpoint(), queued.from);
         auto pi = ngtcp2_pkt_info{};
         const auto rv =
             ngtcp2_conn_read_pkt(conn, &path.path, &pi, queued.bytes.data(), queued.bytes.size(), timestamp());
         if (rv == NGTCP2_ERR_DRAINING || rv == NGTCP2_ERR_CLOSING) {
            fail_all();
            co_return;
         }
         if (rv != 0) {
            fail_all();
            auto message = ngtcp2_read_error_message(conn, rv);
            message += "; packet_size=";
            message += std::to_string(queued.bytes.size());
            throw_engine(engine_error_kind::internal_error, std::move(message));
         }
         metrics.packets_received.fetch_add(1, std::memory_order_relaxed);
         metrics.bytes_received.fetch_add(queued.bytes.size(), std::memory_order_relaxed);
         co_await drain_send();
      }

      clear.reset();
      schedule_post_ngtcp2_work();
   }

   void start_client_receive_loop() {
      if (receive_loop_started) {
         return;
      }
      receive_loop_started = true;
      auto self = this->self.lock();
      if (!self) {
         return;
      }
      asio::co_spawn(
          strand,
          [self]() -> asio::awaitable<void> {
             while (!self->closing && !self->canceled) {
                auto packet = std::vector<std::uint8_t>(65536);
                auto from = udp::endpoint{};
                boost::system::error_code ec;
                const auto nread = co_await self->socket->async_receive_from(
                    asio::buffer(packet), from, asio::redirect_error(asio::use_awaitable, ec));
                if (ec) {
                   if (ec != asio::error::operation_aborted && !self->closing) {
                      self->fail_all();
                   }
                   co_return;
                }
                packet.resize(nread);
                co_await self->handle_packet(std::move(packet), std::move(from));
             }
          },
          asio::detached);
   }
};

namespace {

int get_new_connection_id_cb(ngtcp2_conn*, ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, std::size_t cidlen,
                             void* user_data) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   if (connection == nullptr) {
      return NGTCP2_ERR_CALLBACK_FAILURE;
   }
   cid->datalen = cidlen;
   if (!fill_random({cid->data, cidlen})) {
      return NGTCP2_ERR_CALLBACK_FAILURE;
   }
   if (ngtcp2_crypto_generate_stateless_reset_token(token->data, connection->reset_secret.data(),
                                                    connection->reset_secret.size(), cid) != 0) {
      return NGTCP2_ERR_CALLBACK_FAILURE;
   }
   if (connection->local_connection_id_issued_hook) {
      try {
         connection->local_connection_id_issued_hook(*cid);
      } catch (...) {
         return NGTCP2_ERR_CALLBACK_FAILURE;
      }
   }
   return 0;
}

int remove_connection_id_cb(ngtcp2_conn*, const ngtcp2_cid* cid, void* user_data) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   if (connection == nullptr || cid == nullptr) {
      return 0;
   }
   if (connection->local_connection_id_retired_hook) {
      try {
         connection->local_connection_id_retired_hook(*cid);
      } catch (...) {
         return NGTCP2_ERR_CALLBACK_FAILURE;
      }
   }
   return 0;
}

ngtcp2_conn* get_conn_cb(ngtcp2_crypto_conn_ref* conn_ref) {
   auto* connection = static_cast<engine_connection::impl*>(conn_ref->user_data);
   return connection->conn;
}

int handshake_completed_cb(ngtcp2_conn*, void* user_data) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   try {
      if (connection->server_side) {
         connection->verify_peer(connection->peer_security);
      }
      connection->complete_handshake();
   } catch (const engine_failure&) {
      connection->fail_all();
      return NGTCP2_ERR_CALLBACK_FAILURE;
   }
   return 0;
}

int stream_open_cb(ngtcp2_conn* conn, std::int64_t stream_id, void* user_data) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   auto stream = connection->ensure_stream(stream_id);
   connection->accepted_streams.push_back(stream);
   connection->metrics.streams_accepted.fetch_add(1, std::memory_order_relaxed);
   (void)ngtcp2_conn_set_stream_user_data(conn, stream_id, stream.get());
   wake(connection->accept_stream_waiters);
   return 0;
}

int recv_stream_data_cb(ngtcp2_conn* conn, std::uint32_t flags, std::int64_t stream_id, std::uint64_t offset,
                        const std::uint8_t* data, std::size_t datalen, void* user_data, void* stream_user_data) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   auto stream = stream_user_data == nullptr ? connection->ensure_stream(stream_id) : connection->streams.at(stream_id);
   if (datalen > 0) {
      stream->inbound_segments[offset] = std::vector<std::uint8_t>{data, data + datalen};
      while (true) {
         auto it = stream->inbound_segments.find(stream->recv_next_offset);
         if (it == stream->inbound_segments.end()) {
            break;
         }
         stream->recv_next_offset += it->second.size();
         stream->inbound_ready.push_back(std::move(it->second));
         stream->inbound_segments.erase(it);
      }
      ngtcp2_conn_extend_max_stream_offset(conn, stream_id, datalen);
      ngtcp2_conn_extend_max_offset(conn, datalen);
      connection->metrics.bytes_received.fetch_add(datalen, std::memory_order_relaxed);
      connection->metrics.frames_received.fetch_add(1, std::memory_order_relaxed);
   }
   if ((flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0) {
      stream->remote_read_closed = true;
      connection->update_active_stream_metrics();
   }
   wake(stream->read_waiters);
   return 0;
}

int acked_stream_data_offset_cb(ngtcp2_conn*, std::int64_t stream_id, std::uint64_t offset, std::uint64_t datalen,
                                void* user_data, void*) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   auto it = connection->streams.find(stream_id);
   if (it == connection->streams.end()) {
      return 0;
   }
   auto& stream = it->second;
   const auto ack_end = offset + datalen;
   while (!stream->retained.empty()) {
      auto& write = stream->retained.front();
      const auto end = write.base_offset + write.data.size();
      if (end > ack_end) {
         break;
      }
      const auto queued_bytes = connection->metrics.queued_bytes.load(std::memory_order_relaxed);
      if (queued_bytes >= write.data.size()) {
         connection->metrics.queued_bytes.store(queued_bytes - write.data.size(), std::memory_order_relaxed);
      } else {
         connection->metrics.queued_bytes.store(0, std::memory_order_relaxed);
      }
      stream->retained.pop_front();
   }
   return 0;
}

int stream_close_cb(ngtcp2_conn* conn, std::uint32_t, std::int64_t stream_id, std::uint64_t, void* user_data, void*) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   if (auto it = connection->streams.find(stream_id); it != connection->streams.end()) {
      it->second->closed = true;
      if (ngtcp2_is_bidi_stream(stream_id) && ngtcp2_conn_is_local_stream(conn, stream_id) == 0) {
         ngtcp2_conn_extend_max_streams_bidi(conn, 1);
      }
      wake(it->second->read_waiters);
      wake(it->second->write_waiters);
      connection->update_active_stream_metrics();
   }
   return 0;
}

int stream_reset_cb(ngtcp2_conn* conn, std::int64_t stream_id, std::uint64_t, std::uint64_t, void* user_data, void*) {
   auto* connection = static_cast<engine_connection::impl*>(user_data);
   if (auto it = connection->streams.find(stream_id); it != connection->streams.end()) {
      it->second->reset = true;
      if (ngtcp2_is_bidi_stream(stream_id) && ngtcp2_conn_is_local_stream(conn, stream_id) == 0) {
         ngtcp2_conn_extend_max_streams_bidi(conn, 1);
      }
      connection->metrics.streams_reset.fetch_add(1, std::memory_order_relaxed);
      wake(it->second->read_waiters);
      wake(it->second->write_waiters);
      connection->update_active_stream_metrics();
   }
   return 0;
}

[[nodiscard]] ngtcp2_callbacks client_callbacks() {
   return ngtcp2_callbacks{
       .client_initial = ngtcp2_crypto_client_initial_cb,
       .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
       .handshake_completed = handshake_completed_cb,
       .encrypt = ngtcp2_crypto_encrypt_cb,
       .decrypt = ngtcp2_crypto_decrypt_cb,
       .hp_mask = ngtcp2_crypto_hp_mask_cb,
       .recv_stream_data = recv_stream_data_cb,
       .acked_stream_data_offset = acked_stream_data_offset_cb,
       .stream_open = stream_open_cb,
       .stream_close = stream_close_cb,
       .recv_retry = ngtcp2_crypto_recv_retry_cb,
       .rand = rand_cb,
       .remove_connection_id = remove_connection_id_cb,
       .update_key = ngtcp2_crypto_update_key_cb,
       .stream_reset = stream_reset_cb,
       .handshake_confirmed = handshake_completed_cb,
       .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
       .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
       .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
       .get_new_connection_id2 = get_new_connection_id_cb,
       .get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb,
   };
}

[[nodiscard]] ngtcp2_callbacks server_callbacks() {
   return ngtcp2_callbacks{
       .recv_client_initial = ngtcp2_crypto_recv_client_initial_cb,
       .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
       .handshake_completed = handshake_completed_cb,
       .encrypt = ngtcp2_crypto_encrypt_cb,
       .decrypt = ngtcp2_crypto_decrypt_cb,
       .hp_mask = ngtcp2_crypto_hp_mask_cb,
       .recv_stream_data = recv_stream_data_cb,
       .acked_stream_data_offset = acked_stream_data_offset_cb,
       .stream_open = stream_open_cb,
       .stream_close = stream_close_cb,
       .rand = rand_cb,
       .remove_connection_id = remove_connection_id_cb,
       .update_key = ngtcp2_crypto_update_key_cb,
       .stream_reset = stream_reset_cb,
       .handshake_confirmed = handshake_completed_cb,
       .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
       .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
       .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
       .get_new_connection_id2 = get_new_connection_id_cb,
       .get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb,
   };
}

void configure_settings(ngtcp2_settings& settings) {
   ngtcp2_settings_default(&settings);
   settings.initial_ts = timestamp();
   settings.cc_algo = NGTCP2_CC_ALGO_CUBIC;
}

void configure_params(ngtcp2_transport_params& params, const engine_transport_limits& limits,
                      std::chrono::milliseconds idle_timeout) {
   ngtcp2_transport_params_default(&params);
   params.initial_max_stream_data_bidi_local = 16 * 1024 * 1024;
   params.initial_max_stream_data_bidi_remote = 16 * 1024 * 1024;
   params.initial_max_stream_data_uni = 16 * 1024 * 1024;
   params.initial_max_data = 64 * 1024 * 1024;
   params.initial_max_streams_bidi = limits.max_streams_per_connection;
   params.initial_max_streams_uni = 16;
   params.max_udp_payload_size = max_udp_payload_size;
   params.max_idle_timeout =
       static_cast<ngtcp2_duration>(std::chrono::duration_cast<std::chrono::nanoseconds>(idle_timeout).count());
   params.active_connection_id_limit = 2;
   params.disable_active_migration = 1;
   params.grease_quic_bit = 1;
}

[[nodiscard]] ngtcp2_cid random_cid(std::size_t len = cid_length) {
   auto cid = ngtcp2_cid{};
   cid.datalen = len;
   if (!fill_random({cid.data, cid.datalen})) {
      throw_engine(engine_error_kind::tls_failed, "failed to generate QUIC connection id");
   }
   return cid;
}

void configure_client_tls(engine_connection::impl& connection, const engine_endpoint& remote,
                          const engine_client_options& options) {
   connection.peer_security = options.security;
   connection.ssl_ctx.reset(SSL_CTX_new(TLS_client_method()));
   if (!connection.ssl_ctx) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   if (options.security.verify_peer && !options.security.expected_sha256_fingerprint && !options.security.verifier) {
      SSL_CTX_set_verify(connection.ssl_ctx.get(), SSL_VERIFY_PEER, nullptr);
      configure_default_trust(connection.ssl_ctx.get(), options.security);
   } else {
      SSL_CTX_set_verify(connection.ssl_ctx.get(), SSL_VERIFY_NONE, nullptr);
   }
   if (!options.certificate_pem.empty()) {
      auto cert = load_certificate(options.certificate_pem);
      auto key = load_private_key(options.private_key_pem);
      if (SSL_CTX_use_certificate(connection.ssl_ctx.get(), cert.get()) != 1 ||
          SSL_CTX_use_PrivateKey(connection.ssl_ctx.get(), key.get()) != 1 ||
          SSL_CTX_check_private_key(connection.ssl_ctx.get()) != 1) {
         throw_engine(engine_error_kind::tls_failed, openssl_error());
      }
   }
   connection.ssl.reset(SSL_new(connection.ssl_ctx.get()));
   if (!connection.ssl) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   if (ngtcp2_crypto_ossl_init() != 0) {
      throw_engine(engine_error_kind::tls_failed, "ngtcp2 OpenSSL crypto backend initialization failed");
   }

   if (ngtcp2_crypto_ossl_ctx_new(&connection.ossl_ctx, nullptr) != 0) {
      throw_engine(engine_error_kind::tls_failed, "failed to allocate ngtcp2 OpenSSL context");
   }
   ngtcp2_crypto_ossl_ctx_set_ssl(connection.ossl_ctx, connection.ssl.get());
   if (ngtcp2_crypto_ossl_configure_client_session(connection.ssl.get()) != 0) {
      throw_engine(engine_error_kind::tls_failed,
                   "failed to configure QUIC OpenSSL client session: " + openssl_error());
   }

   connection.conn_ref = ngtcp2_crypto_conn_ref{.get_conn = get_conn_cb, .user_data = &connection};
   SSL_set_app_data(connection.ssl.get(), &connection.conn_ref);
   SSL_set_connect_state(connection.ssl.get());
   const auto alpn = length_prefixed_alpn(options.alpn);
   if (SSL_set_alpn_protos(connection.ssl.get(), alpn.data(), static_cast<unsigned>(alpn.size())) != 0) {
      throw_engine(engine_error_kind::tls_failed, "failed to set QUIC ALPN");
   }
   const auto use_sni = configure_verify_peer_name(connection.ssl.get(), remote.host);
   if (use_sni) {
      const auto* sni = remote.host.empty() ? "localhost" : remote.host.c_str();
      SSL_set_tlsext_host_name(connection.ssl.get(), const_cast<char*>(sni));
   }
}

void configure_server_tls(engine_connection::impl& connection, const engine_server_options& options) {
   connection.peer_security = options.security;
   connection.ssl_ctx.reset(SSL_CTX_new(TLS_server_method()));
   if (!connection.ssl_ctx) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   SSL_CTX_set_max_early_data(connection.ssl_ctx.get(), UINT32_MAX);

   auto cert = load_certificate(options.certificate_pem);
   auto key = load_private_key(options.private_key_pem);
   if (SSL_CTX_use_certificate(connection.ssl_ctx.get(), cert.get()) != 1 ||
       SSL_CTX_use_PrivateKey(connection.ssl_ctx.get(), key.get()) != 1 ||
       SSL_CTX_check_private_key(connection.ssl_ctx.get()) != 1) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }

   SSL_CTX_set_alpn_select_cb(connection.ssl_ctx.get(), select_alpn_cb, const_cast<std::string*>(&options.alpn));
   if (options.security.verify_peer) {
      const auto verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
      if (options.security.expected_sha256_fingerprint || options.security.verifier) {
         SSL_CTX_set_verify(connection.ssl_ctx.get(), verify_mode, accept_any_certificate_cb);
      } else {
         SSL_CTX_set_verify(connection.ssl_ctx.get(), verify_mode, nullptr);
         configure_default_trust(connection.ssl_ctx.get(), options.security);
      }
   } else {
      SSL_CTX_set_verify(connection.ssl_ctx.get(), SSL_VERIFY_NONE, nullptr);
   }

   connection.ssl.reset(SSL_new(connection.ssl_ctx.get()));
   if (!connection.ssl) {
      throw_engine(engine_error_kind::tls_failed, openssl_error());
   }
   if (ngtcp2_crypto_ossl_init() != 0) {
      throw_engine(engine_error_kind::tls_failed, "ngtcp2 OpenSSL crypto backend initialization failed");
   }
   if (ngtcp2_crypto_ossl_ctx_new(&connection.ossl_ctx, nullptr) != 0) {
      throw_engine(engine_error_kind::tls_failed, "failed to allocate ngtcp2 OpenSSL context");
   }
   ngtcp2_crypto_ossl_ctx_set_ssl(connection.ossl_ctx, connection.ssl.get());
   if (ngtcp2_crypto_ossl_configure_server_session(connection.ssl.get()) != 0) {
      throw_engine(engine_error_kind::tls_failed,
                   "failed to configure QUIC OpenSSL server session: " + openssl_error());
   }
   if (options.security.verify_peer) {
      const auto verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
      SSL_set_verify(connection.ssl.get(), verify_mode,
                     (options.security.expected_sha256_fingerprint || options.security.verifier)
                         ? accept_any_certificate_cb
                         : nullptr);
   } else {
      SSL_set_verify(connection.ssl.get(), SSL_VERIFY_NONE, nullptr);
   }

   connection.conn_ref = ngtcp2_crypto_conn_ref{.get_conn = get_conn_cb, .user_data = &connection};
   SSL_set_app_data(connection.ssl.get(), &connection.conn_ref);
   SSL_set_accept_state(connection.ssl.get());
}

} // namespace

engine_stream::engine_stream(std::shared_ptr<impl> impl_value) : impl_(std::move(impl_value)) {}

std::int64_t engine_stream::id() const noexcept {
   return impl_ ? impl_->id : -1;
}

boost::asio::awaitable<void> engine_stream::async_write(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      throw_engine(engine_error_kind::stream_closed, "invalid QUIC stream");
   }
   auto connection = impl_->connection.lock();
   if (!connection) {
      throw_engine(engine_error_kind::connection_closed, "QUIC connection is closed");
   }
   co_await asio::dispatch(connection->strand, asio::use_awaitable);
   if (impl_->reset) {
      throw_engine(engine_error_kind::stream_reset, "QUIC stream is reset");
   }
   if (impl_->local_write_closed || impl_->closed) {
      throw_engine(engine_error_kind::stream_closed, "QUIC stream write side is closed");
   }
   if (connection->metrics.queued_bytes.load(std::memory_order_relaxed) + bytes.size() >
       connection->limits.max_queued_bytes) {
      connection->metrics.backpressure_rejections.fetch_add(1, std::memory_order_relaxed);
      throw_engine(engine_error_kind::backpressure_rejected, "QUIC stream write queue exceeds max_queued_bytes");
   }
   impl_->outbound.push_back(engine_stream::impl::pending_write{
       .data = std::vector<std::uint8_t>{bytes.begin(), bytes.end()},
   });
   connection->metrics.queued_bytes.fetch_add(bytes.size(), std::memory_order_relaxed);
   connection->metrics.frames_sent.fetch_add(1, std::memory_order_relaxed);
   asio::co_spawn(
       connection->strand,
       [connection]() -> asio::awaitable<void> {
          try {
             co_await connection->drain_send();
          } catch (const engine_failure&) {
             connection->fail_all();
          }
       },
       asio::detached);
   co_await asio::post(connection->strand, asio::use_awaitable);
}

boost::asio::awaitable<std::vector<std::uint8_t>> engine_stream::async_read() {
   if (!impl_) {
      throw_engine(engine_error_kind::stream_closed, "invalid QUIC stream");
   }
   auto connection = impl_->connection.lock();
   if (!connection) {
      throw_engine(engine_error_kind::connection_closed, "QUIC connection is closed");
   }
   co_await asio::dispatch(connection->strand, asio::use_awaitable);
   while (impl_->inbound_ready.empty() && !impl_->remote_read_closed && !impl_->reset && !impl_->closed &&
          !connection->closing && !connection->canceled) {
      auto timer = std::make_shared<asio::steady_timer>(connection->strand);
      timer->expires_after(std::chrono::minutes{10});
      impl_->read_waiters.emplace_back(timer);
      boost::system::error_code ec;
      co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
   }
   if (!impl_->inbound_ready.empty()) {
      auto out = std::move(impl_->inbound_ready.front());
      impl_->inbound_ready.pop_front();
      co_return out;
   }
   if (impl_->reset) {
      throw_engine(engine_error_kind::stream_reset, "QUIC stream was reset while reading");
   }
   if (connection->canceled) {
      throw_engine(engine_error_kind::canceled, "QUIC connection was canceled while reading");
   }
   if (connection->closing) {
      throw_engine(engine_error_kind::connection_closed, "QUIC connection closed while reading");
   }
   throw_engine(engine_error_kind::stream_closed, "QUIC stream read side is closed");
}

boost::asio::awaitable<void> engine_stream::async_close() {
   if (!impl_) {
      co_return;
   }
   auto connection = impl_->connection.lock();
   if (!connection) {
      co_return;
   }
   co_await asio::dispatch(connection->strand, asio::use_awaitable);
   if (!impl_->local_write_closed && !impl_->reset && !impl_->closed) {
      impl_->outbound.push_back(engine_stream::impl::pending_write{.fin = true});
      co_await connection->drain_send();
      connection->update_active_stream_metrics();
   }
}

void engine_stream::cancel() {
   if (!impl_) {
      return;
   }
   auto connection = impl_->connection.lock();
   if (!connection) {
      return;
   }
   auto stream = impl_;
   asio::dispatch(connection->strand, [connection, stream] {
      if (stream->reset || stream->closed) {
         return;
      }
      stream->reset = true;
      stream->outbound.clear();
      stream->retained.clear();
      wake(stream->read_waiters);
      wake(stream->write_waiters);
      connection->metrics.streams_reset.fetch_add(1, std::memory_order_relaxed);
      connection->update_active_stream_metrics();
   });
}

engine_connection::engine_connection(std::shared_ptr<impl> impl_value) : impl_(std::move(impl_value)) {}

engine_connection::~engine_connection() = default;

engine_connection_metrics engine_connection::metrics() const {
   return impl_ ? impl_->metrics.snapshot() : engine_connection_metrics{};
}

engine_endpoint engine_connection::local_endpoint() const {
   return impl_ ? from_udp_endpoint(impl_->local_endpoint()) : engine_endpoint{};
}

engine_endpoint engine_connection::remote_endpoint() const {
   return impl_ ? from_udp_endpoint(impl_->remote_endpoint) : engine_endpoint{};
}

std::optional<engine_peer_certificate> engine_connection::peer_certificate() const {
   if (!impl_ || !impl_->ssl) {
      return std::nullopt;
   }
   auto cert = x509_ptr{SSL_get1_peer_certificate(impl_->ssl.get())};
   if (!cert) {
      return std::nullopt;
   }
   auto der = der_from_certificate(cert.get());
   auto peer = engine_peer_certificate{.der = std::move(der)};
   peer.sha256_fingerprint = engine_sha256_fingerprint(peer.der);
   return peer;
}

boost::asio::awaitable<std::shared_ptr<engine_stream>> engine_connection::async_open_stream() {
   if (!impl_) {
      throw_engine(engine_error_kind::connection_closed, "invalid QUIC connection");
   }
   co_await asio::dispatch(impl_->strand, asio::use_awaitable);
   if (impl_->closing || impl_->canceled) {
      throw_engine(engine_error_kind::connection_closed, "QUIC connection is closed");
   }
   if (impl_->active_stream_count() >= impl_->limits.max_streams_per_connection) {
      impl_->metrics.backpressure_rejections.fetch_add(1, std::memory_order_relaxed);
      throw_engine(engine_error_kind::backpressure_rejected, "QUIC max streams exceeded");
   }
   auto stream_id = std::int64_t{-1};
   auto stream_impl = std::make_shared<engine_stream::impl>(-1);
   stream_impl->connection = impl_;
   const auto rv = ngtcp2_conn_open_bidi_stream(impl_->conn, &stream_id, stream_impl.get());
   if (rv != 0) {
      throw_engine(engine_error_kind::backpressure_rejected,
                   std::string{"ngtcp2_conn_open_bidi_stream failed: "} + ngtcp2_strerror(rv));
   }
   stream_impl->id = stream_id;
   impl_->streams.emplace(stream_id, stream_impl);
   impl_->update_active_stream_metrics();
   impl_->metrics.streams_opened.fetch_add(1, std::memory_order_relaxed);
   co_await impl_->drain_send();
   co_return std::shared_ptr<engine_stream>{new engine_stream{std::move(stream_impl)}};
}

boost::asio::awaitable<std::shared_ptr<engine_stream>> engine_connection::async_accept_stream() {
   if (!impl_) {
      throw_engine(engine_error_kind::connection_closed, "invalid QUIC connection");
   }
   co_await asio::dispatch(impl_->strand, asio::use_awaitable);
   while (impl_->accepted_streams.empty() && !impl_->closing && !impl_->canceled) {
      auto timer = std::make_shared<asio::steady_timer>(impl_->strand);
      timer->expires_after(std::chrono::minutes{10});
      impl_->accept_stream_waiters.emplace_back(timer);
      boost::system::error_code ec;
      co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
   }
   if (impl_->accepted_streams.empty()) {
      throw_engine(engine_error_kind::connection_closed, "QUIC connection closed before accepting stream");
   }
   auto stream = std::move(impl_->accepted_streams.front());
   impl_->accepted_streams.pop_front();
   co_return std::shared_ptr<engine_stream>{new engine_stream{std::move(stream)}};
}

boost::asio::awaitable<void> engine_connection::async_close() {
   if (!impl_) {
      co_return;
   }
   co_await asio::dispatch(impl_->strand, asio::use_awaitable);
   if (impl_->closing) {
      co_return;
   }
   impl_->metrics.connections_closed.fetch_add(1, std::memory_order_relaxed);
   impl_->close_transport(!impl_->server_side);
}

void engine_connection::cancel() {
   if (!impl_) {
      return;
   }
   asio::post(impl_->strand, [impl = impl_] {
      impl->metrics.cancellations.fetch_add(1, std::memory_order_relaxed);
      impl->fail_all();
      impl->cancel_transport_io(!impl->server_side);
   });
}

struct engine_connector::impl {
   struct active_connect {
      enum class state_value : std::uint8_t {
         pending,
         completed,
         timed_out,
         canceled,
      };

      std::atomic<state_value> state{state_value::pending};
      std::mutex mutex;
      std::shared_ptr<udp::resolver> resolver;
      std::weak_ptr<udp::socket> socket;
      std::weak_ptr<engine_connection::impl> connection;

      [[nodiscard]] bool mark_timed_out() noexcept {
         auto expected = state_value::pending;
         return state.compare_exchange_strong(expected, state_value::timed_out, std::memory_order_acq_rel);
      }

      [[nodiscard]] bool mark_canceled() noexcept {
         auto expected = state_value::pending;
         return state.compare_exchange_strong(expected, state_value::canceled, std::memory_order_acq_rel);
      }

      [[nodiscard]] bool finish() noexcept {
         auto expected = state_value::pending;
         return state.compare_exchange_strong(expected, state_value::completed, std::memory_order_acq_rel);
      }

      [[nodiscard]] bool timed_out() const noexcept {
         return state.load(std::memory_order_acquire) == state_value::timed_out;
      }

      [[nodiscard]] bool canceled() const noexcept {
         return state.load(std::memory_order_acquire) == state_value::canceled;
      }

      void cancel_io() {
         if (!mark_canceled()) {
            return;
         }
         auto resolver_value = std::shared_ptr<udp::resolver>{};
         auto socket_value = std::shared_ptr<udp::socket>{};
         auto connection_value = std::shared_ptr<engine_connection::impl>{};
         {
            auto lock = std::scoped_lock{mutex};
            resolver_value = resolver;
            socket_value = socket.lock();
            connection_value = connection.lock();
         }
         if (resolver_value) {
            try {
               resolver_value->cancel();
            } catch (...) {
               // Resolver cancellation is best-effort once the caller has canceled connect.
            }
         }
         if (socket_value) {
            auto ignored = boost::system::error_code{};
            socket_value->cancel(ignored);
         }
         if (connection_value) {
            asio::post(connection_value->strand, [connection_value] {
               connection_value->metrics.cancellations.fetch_add(1, std::memory_order_relaxed);
               connection_value->fail_all();
               connection_value->cancel_transport_io(true);
            });
         }
      }
   };

   explicit impl(boost::asio::io_context& context_value) : context(context_value) {}

   [[nodiscard]] bool valid() const noexcept {
      return !canceled.load(std::memory_order_acquire);
   }

   [[nodiscard]] std::shared_ptr<active_connect> track_connect() {
      if (!valid()) {
         throw_engine(engine_error_kind::canceled, "QUIC connector is canceled");
      }
      auto connect = std::make_shared<active_connect>();
      auto lock = std::scoped_lock{mutex};
      active.erase(std::remove_if(active.begin(), active.end(), [](const auto& value) { return value.expired(); }),
                   active.end());
      active.push_back(connect);
      return connect;
   }

   void cancel() {
      canceled.store(true, std::memory_order_release);
      auto connections = std::vector<std::shared_ptr<active_connect>>{};
      {
         auto lock = std::scoped_lock{mutex};
         connections.reserve(active.size());
         for (auto& value : active) {
            if (auto connect = value.lock()) {
               connections.push_back(std::move(connect));
            }
         }
      }
      for (auto& connect : connections) {
         connect->cancel_io();
      }
   }

   boost::asio::io_context& context;
   std::mutex mutex;
   std::vector<std::weak_ptr<active_connect>> active;
   std::atomic_bool canceled = false;
};

engine_connector::engine_connector(boost::asio::io_context& context) : impl_(std::make_shared<impl>(context)) {}

boost::asio::awaitable<std::shared_ptr<engine_connection>>
engine_connector::async_connect(engine_endpoint remote, engine_client_options options) {
   if (!impl_ || !impl_->valid()) {
      throw_engine(engine_error_kind::canceled, "QUIC connector is canceled");
   }
   const auto executor = co_await asio::this_coro::executor;
   const auto connect_started = std::chrono::steady_clock::now();
   auto resolver = std::make_shared<udp::resolver>(executor);
   auto connect_timer = std::make_shared<asio::steady_timer>(executor);
   auto active_connect = impl_->track_connect();
   {
      auto lock = std::scoped_lock{active_connect->mutex};
      active_connect->resolver = resolver;
   }
   connect_timer->expires_after(options.connect_timeout);
   connect_timer->async_wait([connect_timer, active_connect](boost::system::error_code ec) {
      if (ec) {
         return;
      }
      if (!active_connect->mark_timed_out()) {
         return;
      }
      auto resolver = std::shared_ptr<udp::resolver>{};
      auto connection = std::shared_ptr<engine_connection::impl>{};
      {
         auto lock = std::scoped_lock{active_connect->mutex};
         resolver = active_connect->resolver;
         connection = active_connect->connection.lock();
      }
      if (connection) {
         asio::post(connection->strand, [connection] {
            connection->metrics.timeouts.fetch_add(1, std::memory_order_relaxed);
            connection->fail_all();
         });
         return;
      }
      if (resolver) {
         try {
            resolver->cancel();
         } catch (...) {
            // Resolver cancellation is best-effort after the connect deadline fires.
         }
      }
   });
   auto finish_connect_or_throw = [&] {
      if (connect_failpoint_enabled("timeout_before_pre_connection_error_finish")) {
         (void)active_connect->mark_timed_out();
      }
      if (!active_connect->finish()) {
         connect_timer->cancel();
         if (active_connect->canceled()) {
            throw_engine(engine_error_kind::canceled, "QUIC client connect canceled");
         }
         throw_engine(engine_error_kind::connect_timeout, "QUIC client connect timed out");
      }
      connect_timer->cancel();
   };
   boost::system::error_code ec;
   auto resolved = co_await resolver->async_resolve(remote.host, std::to_string(remote.port),
                                                    asio::redirect_error(asio::use_awaitable, ec));
   {
      auto lock = std::scoped_lock{active_connect->mutex};
      active_connect->resolver.reset();
   }
   if (active_connect->canceled()) {
      connect_timer->cancel();
      throw_engine(engine_error_kind::canceled, "QUIC endpoint resolution canceled");
   }
   if (active_connect->timed_out()) {
      connect_timer->cancel();
      throw_engine(engine_error_kind::connect_timeout, "QUIC endpoint resolution timed out");
   }
   if (ec || resolved.empty()) {
      finish_connect_or_throw();
      throw_engine(engine_error_kind::invalid_endpoint, "failed to resolve QUIC endpoint: " + ec.message());
   }
   auto remote_endpoint = *resolved.begin();
   auto socket = std::make_shared<udp::socket>(impl_->context);
   {
      auto lock = std::scoped_lock{active_connect->mutex};
      active_connect->socket = socket;
   }
   socket->open(remote_endpoint.endpoint().protocol(), ec);
   if (ec) {
      finish_connect_or_throw();
      throw_engine(engine_error_kind::internal_error, "failed to open QUIC UDP socket: " + ec.message());
   }
   socket->bind(udp::endpoint{remote_endpoint.endpoint().protocol(), 0}, ec);
   if (ec) {
      finish_connect_or_throw();
      throw_engine(engine_error_kind::internal_error, "failed to bind QUIC UDP socket: " + ec.message());
   }
   if (active_connect->canceled()) {
      connect_timer->cancel();
      throw_engine(engine_error_kind::canceled, "QUIC client connect canceled");
   }
   if (active_connect->timed_out()) {
      connect_timer->cancel();
      throw_engine(engine_error_kind::connect_timeout, "QUIC client connect timed out");
   }

   auto connection_impl =
       std::make_shared<engine_connection::impl>(impl_->context, socket, remote_endpoint.endpoint(), options.limits);
   connection_impl->self = connection_impl;
   connection_impl->metrics.connections_opened.store(1, std::memory_order_relaxed);
   connection_impl->metrics.handshakes_started.store(1, std::memory_order_relaxed);
   connection_impl->server_side = false;
   {
      auto lock = std::scoped_lock{active_connect->mutex};
      active_connect->connection = connection_impl;
   }
   if (active_connect->canceled()) {
      connect_timer->cancel();
      co_await asio::dispatch(connection_impl->strand, asio::use_awaitable);
      connection_impl->fail_all();
      connection_impl->cancel_transport_io(true);
      throw_engine(engine_error_kind::canceled, "QUIC client connect canceled");
   }
   if (active_connect->timed_out()) {
      connect_timer->cancel();
      co_await asio::dispatch(connection_impl->strand, asio::use_awaitable);
      connection_impl->fail_all();
      throw_engine(engine_error_kind::connect_timeout, "QUIC client connect timed out");
   }

   auto connect_error = std::exception_ptr{};
   auto handshake_limited_by_connect_deadline = false;
   try {
      auto callbacks = client_callbacks();
      auto settings = ngtcp2_settings{};
      auto params = ngtcp2_transport_params{};
      configure_settings(settings);
      configure_params(params, options.limits, options.idle_timeout);

      const auto dcid = random_cid(NGTCP2_MIN_INITIAL_DCIDLEN);
      const auto scid = random_cid(cid_length);
      auto path = make_path(socket->local_endpoint(), remote_endpoint.endpoint());
      const auto rv = ngtcp2_conn_client_new(&connection_impl->conn, &dcid, &scid, &path.path, NGTCP2_PROTO_VER_V1,
                                             &callbacks, &settings, &params, nullptr, connection_impl.get());
      if (rv != 0) {
         throw_engine(engine_error_kind::internal_error,
                      std::string{"ngtcp2_conn_client_new failed: "} + ngtcp2_strerror(rv));
      }

      configure_client_tls(*connection_impl, remote, options);
      ngtcp2_conn_set_tls_native_handle(connection_impl->conn, connection_impl->ossl_ctx);
      connection_impl->start_client_receive_loop();
      asio::co_spawn(
          connection_impl->strand,
          [connection_impl]() -> asio::awaitable<void> {
             try {
                co_await connection_impl->drain_send();
             } catch (const engine_failure&) {
                connection_impl->fail_all();
             }
          },
          asio::detached);
      const auto remaining_connect_timeout = remaining_timeout_budget(connect_started, options.connect_timeout);
      if (remaining_connect_timeout.count() <= 0) {
         throw_engine(engine_error_kind::connect_timeout, "QUIC client connect timed out");
      }
      handshake_limited_by_connect_deadline = remaining_connect_timeout < options.handshake_timeout;
      co_await connection_impl->wait_handshake(std::min(options.handshake_timeout, remaining_connect_timeout));
      if (active_connect->canceled()) {
         throw_engine(engine_error_kind::canceled, "QUIC client connect canceled");
      }
      if (active_connect->timed_out()) {
         throw_engine(engine_error_kind::connect_timeout, "QUIC client connect timed out");
      }
      connection_impl->verify_selected_alpn(options.alpn);
      connection_impl->verify_peer(options.security);
      finish_connect_or_throw();
      connect_timer->cancel();
   } catch (const engine_failure& error) {
      if (active_connect->canceled()) {
         connect_error = std::make_exception_ptr(engine_failure{engine_error_kind::canceled, "QUIC client connect canceled"});
      } else if (active_connect->timed_out() ||
          (error.kind() == engine_error_kind::handshake_timeout && handshake_limited_by_connect_deadline)) {
         connect_error =
             std::make_exception_ptr(engine_failure{engine_error_kind::connect_timeout, "QUIC client connect timed out"});
      } else {
         connect_error = std::current_exception();
      }
   } catch (...) {
      connect_error = std::current_exception();
   }
   if (connect_error) {
      (void)active_connect->finish();
      connect_timer->cancel();
      co_await asio::dispatch(connection_impl->strand, asio::use_awaitable);
      connection_impl->fail_all();
      connection_impl->cancel_transport_io(true);
      std::rethrow_exception(connect_error);
   }
   co_return std::shared_ptr<engine_connection>{new engine_connection{std::move(connection_impl)}};
}

void engine_connector::cancel() {
   if (impl_) {
      impl_->cancel();
   }
}

struct engine_listener::impl {
   impl(boost::asio::io_context& context_value, engine_endpoint endpoint_value, engine_server_options options_value)
       : context(context_value), strand(asio::make_strand(context_value)),
         socket(std::make_shared<udp::socket>(strand)), bind_endpoint(std::move(endpoint_value)),
         options(std::move(options_value)) {}

   boost::asio::io_context& context;
   asio::strand<asio::io_context::executor_type> strand;
   std::shared_ptr<udp::socket> socket;
   engine_endpoint bind_endpoint;
   engine_server_options options;
   stateless_reset_secret reset_secret = random_stateless_reset_secret();
   std::mutex cid_mutex;
   std::unordered_map<std::string, std::shared_ptr<engine_connection::impl>> connections_by_cid;
   std::unordered_map<engine_connection::impl*, std::vector<std::string>> cids_by_connection;
   std::deque<std::shared_ptr<engine_connection>> accepted;
   std::vector<std::weak_ptr<asio::steady_timer>> accept_waiters;
   std::optional<engine_error_kind> pending_accept_error;
   std::string pending_accept_failure_text;
   std::weak_ptr<impl> self;
   bool stopped = false;
   bool receive_started = false;

   void start() {
      if (receive_started) {
         return;
      }
      receive_started = true;
      auto self = this->self.lock();
      if (!self) {
         return;
      }
      asio::co_spawn(
          strand,
          [self]() -> asio::awaitable<void> {
             while (!self->stopped) {
                auto packet = std::vector<std::uint8_t>(65536);
                auto from = udp::endpoint{};
                boost::system::error_code ec;
                const auto nread = co_await self->socket->async_receive_from(
                    asio::buffer(packet), from, asio::redirect_error(asio::use_awaitable, ec));
                if (ec) {
                   co_return;
                }
                packet.resize(nread);
                try {
                   co_await self->handle_packet(std::move(packet), std::move(from));
                } catch (const engine_failure&) {
                   // Malformed/adversarial packets must not permanently stop the listener.
                }
             }
          },
          asio::detached);
   }

   [[nodiscard]] std::shared_ptr<engine_connection::impl> find_connection_by_cid(const std::string& key) {
      auto lock = std::scoped_lock{cid_mutex};
      if (auto it = connections_by_cid.find(key); it != connections_by_cid.end()) {
         return it->second;
      }
      return {};
   }

   [[nodiscard]] std::size_t connection_count() {
      auto lock = std::scoped_lock{cid_mutex};
      return cids_by_connection.size();
   }

   void register_connection_cid(const std::shared_ptr<engine_connection::impl>& connection, std::string key) {
      auto lock = std::scoped_lock{cid_mutex};
      connections_by_cid[key] = connection;
      auto& keys = cids_by_connection[connection.get()];
      if (std::ranges::find(keys, key) == keys.end()) {
         keys.push_back(std::move(key));
      }
   }

   void unregister_connection_cid(engine_connection::impl* connection, std::string key) {
      auto lock = std::scoped_lock{cid_mutex};
      auto cid = connections_by_cid.find(key);
      if (cid != connections_by_cid.end() && cid->second.get() == connection) {
         connections_by_cid.erase(cid);
      }
      auto it = cids_by_connection.find(connection);
      if (it == cids_by_connection.end()) {
         return;
      }
      std::erase(it->second, key);
      if (it->second.empty()) {
         cids_by_connection.erase(it);
      }
   }

   void cleanup_connection(const std::shared_ptr<engine_connection::impl>& connection) {
      const auto has_accept_waiter = std::ranges::any_of(
          accept_waiters, [](const std::weak_ptr<asio::steady_timer>& waiter) { return !waiter.expired(); });
      const auto failed_before_accept =
          has_accept_waiter && !connection->handshake_done && !connection->listener_accept_notified && !stopped;
      auto had_connection_ids = false;
      {
         auto lock = std::scoped_lock{cid_mutex};
         auto it = cids_by_connection.find(connection.get());
         if (it != cids_by_connection.end()) {
            had_connection_ids = true;
            for (const auto& key : it->second) {
               auto cid = connections_by_cid.find(key);
               if (cid != connections_by_cid.end() && cid->second.get() == connection.get()) {
                  connections_by_cid.erase(cid);
               }
            }
            cids_by_connection.erase(it);
         }
      }
      if (failed_before_accept && had_connection_ids) {
         const auto timed_out = connection->metrics.timeouts.load(std::memory_order_relaxed) > 0;
         pending_accept_error = timed_out ? engine_error_kind::handshake_timeout : engine_error_kind::connection_closed;
         pending_accept_failure_text = timed_out ? "QUIC server handshake timed out before accept"
                                                  : "QUIC server connection closed before accept";
         wake(accept_waiters);
      }
   }

   boost::asio::awaitable<void> handle_packet(std::vector<std::uint8_t> packet, udp::endpoint from) {
      auto vcid = ngtcp2_version_cid{};
      auto rv = ngtcp2_pkt_decode_version_cid(&vcid, packet.data(), packet.size(), cid_length);
      if (rv != 0) {
         co_return;
      }
      auto key = cid_key(vcid.dcid, vcid.dcidlen);
      auto connection = std::shared_ptr<engine_connection::impl>{};
      connection = find_connection_by_cid(key);
      if (!connection) {
         auto hd = ngtcp2_pkt_hd{};
         if (ngtcp2_pkt_decode_hd_long(&hd, packet.data(), packet.size()) < 0) {
            co_return;
         }
         connection = create_server_connection(hd, from);
         register_connection_cid(connection, cid_key(hd.dcid.data, hd.dcid.datalen));
         auto local_cid = ngtcp2_cid{};
         ngtcp2_conn_get_scid(connection->conn, &local_cid);
         register_connection_cid(connection, cid_key(local_cid));
      }
      try {
         co_await connection->handle_packet(std::move(packet), std::move(from));
      } catch (const engine_failure&) {
         asio::post(connection->strand, [connection] { connection->fail_all(); });
      }
   }

   [[nodiscard]] std::shared_ptr<engine_connection::impl> create_server_connection(const ngtcp2_pkt_hd& hd,
                                                                                   const udp::endpoint& from) {
      if (connection_count() >= options.limits.max_connections) {
         throw_engine(engine_error_kind::backpressure_rejected, "QUIC listener max connections exceeded");
      }
      auto connection = std::make_shared<engine_connection::impl>(context, socket, from, options.limits);
      connection->self = connection;
      connection->server_side = true;
      connection->reset_secret = reset_secret;
      connection->metrics.connections_opened.store(1, std::memory_order_relaxed);
      connection->metrics.handshakes_started.store(1, std::memory_order_relaxed);
      auto listener_weak = self;
      connection->closed_hook = [listener_weak](std::shared_ptr<engine_connection::impl> closed_connection) {
         auto listener = listener_weak.lock();
         if (!listener) {
            return;
         }
         asio::post(listener->strand, [listener, closed_connection = std::move(closed_connection)] {
            listener->cleanup_connection(closed_connection);
         });
      };
      connection->local_connection_id_issued_hook = [listener_weak,
                                                     connection_weak = std::weak_ptr<engine_connection::impl>{
                                                         connection}](const ngtcp2_cid& cid) {
         auto listener = listener_weak.lock();
         auto connection = connection_weak.lock();
         if (!listener || !connection) {
            return;
         }
         listener->register_connection_cid(connection, cid_key(cid));
      };
      connection->local_connection_id_retired_hook = [listener_weak,
                                                      connection_weak = std::weak_ptr<engine_connection::impl>{
                                                          connection}](const ngtcp2_cid& cid) {
         auto listener = listener_weak.lock();
         auto connection = connection_weak.lock();
         if (!listener || !connection) {
            return;
         }
         listener->unregister_connection_cid(connection.get(), cid_key(cid));
      };
      auto connection_weak = std::weak_ptr<engine_connection::impl>{connection};
      connection->handshake_completed_hook = [listener_weak, connection_weak] {
         auto listener = listener_weak.lock();
         auto connection = connection_weak.lock();
         if (!listener || !connection) {
            return;
         }
         asio::post(listener->strand, [listener, connection] {
            if (listener->stopped) {
               return;
            }
            if (connection->listener_accept_notified) {
               return;
            }
            connection->listener_accept_notified = true;
            listener->accepted.push_back(std::shared_ptr<engine_connection>{new engine_connection{connection}});
            wake(listener->accept_waiters);
         });
      };
      auto handshake_timer = std::make_shared<asio::steady_timer>(connection->strand);
      handshake_timer->expires_after(options.handshake_timeout);
      handshake_timer->async_wait([connection_weak, handshake_timer](boost::system::error_code ec) {
         if (ec) {
            return;
         }
         auto connection = connection_weak.lock();
         if (!connection || connection->handshake_done || connection->closing || connection->canceled) {
            return;
         }
         asio::post(connection->strand, [connection] {
            if (connection->handshake_done || connection->closing || connection->canceled) {
               return;
            }
            connection->metrics.handshakes_failed.fetch_add(1, std::memory_order_relaxed);
            connection->metrics.timeouts.fetch_add(1, std::memory_order_relaxed);
            connection->fail_all();
         });
      });

      auto callbacks = server_callbacks();
      auto settings = ngtcp2_settings{};
      auto params = ngtcp2_transport_params{};
      configure_settings(settings);
      configure_params(params, options.limits, options.idle_timeout);
      params.original_dcid = hd.dcid;
      params.original_dcid_present = 1;
      params.stateless_reset_token_present = 1;

      const auto scid = random_cid(cid_length);
      if (ngtcp2_crypto_generate_stateless_reset_token(params.stateless_reset_token, reset_secret.data(),
                                                       reset_secret.size(), &scid) != 0) {
         throw_engine(engine_error_kind::tls_failed, "failed to generate stateless reset token");
      }
      auto path = make_path(socket->local_endpoint(), from);
      const auto rv = ngtcp2_conn_server_new(&connection->conn, &hd.scid, &scid, &path.path, hd.version, &callbacks,
                                             &settings, &params, nullptr, connection.get());
      if (rv != 0) {
         throw_engine(engine_error_kind::internal_error,
                      std::string{"ngtcp2_conn_server_new failed: "} + ngtcp2_strerror(rv));
      }
      configure_server_tls(*connection, options);
      ngtcp2_conn_set_tls_native_handle(connection->conn, connection->ossl_ctx);
      return connection;
   }
};

engine_listener::engine_listener(boost::asio::io_context& context, engine_endpoint bind_endpoint,
                                 engine_server_options options)
    : impl_(std::make_shared<impl>(context, std::move(bind_endpoint), std::move(options))) {
   impl_->self = impl_;
   auto ec = boost::system::error_code{};
   auto address = impl_->bind_endpoint.host.empty() ? asio::ip::make_address("127.0.0.1")
                                                    : asio::ip::make_address(impl_->bind_endpoint.host, ec);
   if (ec) {
      throw_engine(engine_error_kind::invalid_endpoint, "invalid QUIC listener address: " + ec.message());
   }
   auto endpoint = udp::endpoint{address, impl_->bind_endpoint.port};
   impl_->socket->open(endpoint.protocol(), ec);
   if (ec) {
      throw_engine(engine_error_kind::internal_error, "failed to open QUIC listener socket: " + ec.message());
   }
   impl_->socket->bind(endpoint, ec);
   if (ec) {
      throw_engine(engine_error_kind::internal_error, "failed to bind QUIC listener socket: " + ec.message());
   }
   const auto local = impl_->socket->local_endpoint();
   impl_->bind_endpoint.host = local.address().to_string();
   impl_->bind_endpoint.port = local.port();
   impl_->start();
}

engine_listener::~engine_listener() {
   stop();
}

engine_endpoint engine_listener::local_endpoint() const {
   return impl_ ? impl_->bind_endpoint : engine_endpoint{};
}

boost::asio::awaitable<std::shared_ptr<engine_connection>> engine_listener::async_accept() {
   if (!impl_) {
      throw_engine(engine_error_kind::connection_closed, "invalid QUIC listener");
   }
   co_await asio::dispatch(impl_->strand, asio::use_awaitable);
   while (impl_->accepted.empty() && !impl_->stopped && !impl_->pending_accept_error) {
      auto timer = std::make_shared<asio::steady_timer>(impl_->strand);
      timer->expires_after(std::chrono::minutes{10});
      impl_->accept_waiters.emplace_back(timer);
      boost::system::error_code ec;
      co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
   }
   if (impl_->accepted.empty() && impl_->pending_accept_error) {
      const auto kind = *impl_->pending_accept_error;
      auto message = std::move(impl_->pending_accept_failure_text);
      impl_->pending_accept_error.reset();
      impl_->pending_accept_failure_text.clear();
      throw_engine(kind, message.empty() ? "QUIC listener accept failed" : message);
   }
   if (impl_->accepted.empty()) {
      throw_engine(engine_error_kind::connection_closed, "QUIC listener stopped before accept");
   }
   impl_->pending_accept_error.reset();
   impl_->pending_accept_failure_text.clear();
   auto connection = std::move(impl_->accepted.front());
   impl_->accepted.pop_front();
   co_return connection;
}

void engine_listener::stop() {
   if (!impl_) {
      return;
   }
   asio::post(impl_->strand, [impl = impl_] {
      impl->stopped = true;
      boost::system::error_code ignored;
      impl->socket->cancel(ignored);
      impl->socket->close(ignored);
      wake(impl->accept_waiters);
      auto connections = std::vector<std::shared_ptr<engine_connection::impl>>{};
      connections.reserve(impl->cids_by_connection.size());
      for (const auto& [_, keys] : impl->cids_by_connection) {
         if (keys.empty()) {
            continue;
         }
         if (auto it = impl->connections_by_cid.find(keys.front()); it != impl->connections_by_cid.end()) {
            connections.push_back(it->second);
         }
      }
      for (auto& connection : connections) {
         asio::post(connection->strand, [connection] { connection->fail_all(); });
      }
   });
}

} // namespace fcl::quic::detail
