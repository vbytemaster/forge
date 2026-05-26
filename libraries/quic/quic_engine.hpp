#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fcl::quic::detail {

enum class engine_error_kind {
   invalid_endpoint,
   invalid_options,
   dependency_unavailable,
   connect_timeout,
   handshake_timeout,
   idle_timeout,
   tls_failed,
   peer_verification_failed,
   alpn_mismatch,
   frame_too_large,
   malformed_frame,
   backpressure_rejected,
   connection_closed,
   stream_closed,
   stream_reset,
   canceled,
   internal_error
};

class engine_failure final {
 public:
   engine_failure(engine_error_kind kind, std::string message);

   [[nodiscard]] engine_error_kind kind() const noexcept;
   [[nodiscard]] const char* what() const noexcept;
   [[nodiscard]] const std::string& message() const noexcept;

 private:
   engine_error_kind kind_;
   std::string message_;
};

struct engine_endpoint {
   std::string host;
   std::uint16_t port = 0;
};

struct engine_transport_limits {
   std::size_t max_connections = 1024;
   std::size_t max_streams_per_connection = 256;
   std::size_t max_queued_bytes = 16 * 1024 * 1024;
   std::size_t max_inbound_queued_bytes = 16 * 1024 * 1024;
   std::size_t max_inbound_queued_packets = 4096;
   std::uint64_t max_frame_size = 16 * 1024 * 1024;
};

struct engine_peer_certificate {
   std::vector<std::uint8_t> der;
   std::string sha256_fingerprint;
};

using engine_peer_verifier = std::function<bool(const engine_peer_certificate&)>;

struct engine_security_options {
   bool verify_peer = true;
   std::optional<std::string> expected_sha256_fingerprint;
   std::string trusted_ca_pem;
   engine_peer_verifier verifier;
};

struct engine_client_options {
   std::string alpn = "fcl-p2p/1";
   std::chrono::milliseconds connect_timeout{10'000};
   std::chrono::milliseconds handshake_timeout{10'000};
   std::chrono::milliseconds idle_timeout{30'000};
   engine_transport_limits limits{};
   engine_security_options security{};
   std::string certificate_pem;
   std::string private_key_pem;
};

struct engine_server_options {
   std::string alpn = "fcl-p2p/1";
   std::chrono::milliseconds handshake_timeout{10'000};
   std::chrono::milliseconds idle_timeout{30'000};
   engine_transport_limits limits{};
   engine_security_options security{};
   std::string certificate_pem;
   std::string private_key_pem;
};

struct engine_connection_metrics {
   std::uint64_t connections_opened = 0;
   std::uint64_t connections_closed = 0;
   std::uint64_t handshakes_started = 0;
   std::uint64_t handshakes_completed = 0;
   std::uint64_t handshakes_failed = 0;
   std::uint64_t streams_opened = 0;
   std::uint64_t streams_accepted = 0;
   std::uint64_t streams_reset = 0;
   std::uint64_t frames_sent = 0;
   std::uint64_t frames_received = 0;
   std::uint64_t bytes_sent = 0;
   std::uint64_t bytes_received = 0;
   std::uint64_t packets_sent = 0;
   std::uint64_t packets_received = 0;
   std::uint64_t timeouts = 0;
   std::uint64_t cancellations = 0;
   std::uint64_t backpressure_rejections = 0;
   std::size_t queued_bytes = 0;
   std::size_t active_streams = 0;
   bool closed = false;
};

class engine_stream;
class engine_connection;
class engine_connector;
class engine_listener;

class engine_stream : public std::enable_shared_from_this<engine_stream> {
 public:
   struct impl;

   engine_stream(const engine_stream&) = delete;
   engine_stream& operator=(const engine_stream&) = delete;

   [[nodiscard]] std::int64_t id() const noexcept;

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read();
   boost::asio::awaitable<void> async_close();

 private:
   friend class engine_connection;

   explicit engine_stream(std::shared_ptr<impl> impl_value);

   std::shared_ptr<impl> impl_;
};

class engine_connection : public std::enable_shared_from_this<engine_connection> {
 public:
   struct impl;

   engine_connection(const engine_connection&) = delete;
   engine_connection& operator=(const engine_connection&) = delete;
   ~engine_connection();

   [[nodiscard]] engine_connection_metrics metrics() const;
   [[nodiscard]] std::optional<engine_peer_certificate> peer_certificate() const;
   boost::asio::awaitable<std::shared_ptr<engine_stream>> async_open_stream();
   boost::asio::awaitable<std::shared_ptr<engine_stream>> async_accept_stream();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend class engine_connector;
   friend class engine_listener;
   friend class engine_stream;

   explicit engine_connection(std::shared_ptr<impl> impl_value);

   std::shared_ptr<impl> impl_;
};

class engine_connector {
 public:
   explicit engine_connector(boost::asio::io_context& context);

   boost::asio::awaitable<std::shared_ptr<engine_connection>> async_connect(engine_endpoint remote,
                                                                            engine_client_options options);

 private:
   boost::asio::io_context& context_;
};

class engine_listener {
 public:
   struct impl;

   engine_listener(boost::asio::io_context& context, engine_endpoint bind_endpoint, engine_server_options options);
   ~engine_listener();

   [[nodiscard]] engine_endpoint local_endpoint() const;
   boost::asio::awaitable<std::shared_ptr<engine_connection>> async_accept();
   void stop();

 private:
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] std::string engine_sha256_fingerprint(std::span<const std::uint8_t> data);
[[nodiscard]] std::string normalize_engine_sha256_fingerprint(std::string_view value);

} // namespace fcl::quic::detail
