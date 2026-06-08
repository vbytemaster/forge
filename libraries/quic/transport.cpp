module;

#include <fcl/exceptions/macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

module fcl.quic.transport;

import fcl.asio.runtime;
import fcl.quic.exceptions;

namespace fcl::quic {
namespace {

class quic_stream_concept final : public fcl::transport::detail::stream_concept {
 public:
   explicit quic_stream_concept(stream value) : value_(std::move(value)) {}

   [[nodiscard]] bool valid() const noexcept override {
      return value_.valid();
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return value_.id();
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      co_await value_.async_write(bytes);
   }

   boost::asio::awaitable<void> async_write_chunk(fcl::transport::chunk bytes) override {
      co_await value_.async_write(bytes.bytes());
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      co_return co_await value_.async_read();
   }

   boost::asio::awaitable<fcl::transport::chunk> async_read_chunk() override {
      co_return fcl::transport::chunk{co_await value_.async_read()};
   }

   boost::asio::awaitable<void> async_close() override {
      co_await value_.async_close();
   }

   void cancel() override {
      value_.cancel();
   }

 private:
   stream value_;
};

class quic_session_concept final : public fcl::transport::detail::session_concept {
 public:
   explicit quic_session_concept(connection value) : value_(std::move(value)) {}

   [[nodiscard]] bool valid() const noexcept override {
      return value_.valid();
   }

   boost::asio::awaitable<fcl::transport::stream> async_open_stream() override {
      co_return as_transport_stream(co_await value_.async_open_stream());
   }

   boost::asio::awaitable<fcl::transport::stream> async_accept_stream() override {
      co_return as_transport_stream(co_await value_.async_accept_stream());
   }

   boost::asio::awaitable<void> async_close() override {
      co_await value_.async_close();
   }

   void cancel() override {
      value_.cancel();
   }

 private:
   connection value_;
};

[[nodiscard]] bool same_limits(const fcl::transport::limits& left, const fcl::transport::limits& right) noexcept {
   return left.max_connections == right.max_connections &&
          left.max_streams_per_connection == right.max_streams_per_connection &&
          left.max_queued_bytes == right.max_queued_bytes &&
          left.max_inbound_queued_bytes == right.max_inbound_queued_bytes &&
          left.max_inbound_queued_packets == right.max_inbound_queued_packets &&
          left.max_frame_size == right.max_frame_size;
}

[[nodiscard]] bool has_custom_limits(const fcl::transport::limits& value) noexcept {
   return !same_limits(value, fcl::transport::limits{});
}

[[noreturn]] void throw_invalid_transport_endpoint(const fcl::transport::endpoint& value, std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, std::move(message),
                       fcl::exceptions::ctx("host", value.host),
                       fcl::exceptions::ctx("port", value.port),
                       fcl::exceptions::ctx("protocol", static_cast<int>(value.protocol)));
}

[[nodiscard]] endpoint validate_connect_endpoint(const fcl::transport::endpoint& value) {
   if (value.protocol != fcl::transport::endpoint::protocol_kind::quic_v1) {
      throw_invalid_transport_endpoint(value, "QUIC transport connector requires quic_v1 endpoint protocol");
   }
   if (value.host.empty()) {
      throw_invalid_transport_endpoint(value, "QUIC transport connector requires non-empty host");
   }
   if (value.port == 0) {
      throw_invalid_transport_endpoint(value, "QUIC transport connector requires non-zero remote port");
   }
   return endpoint{.host = value.host, .port = value.port};
}

[[nodiscard]] endpoint validate_listen_endpoint(const fcl::transport::endpoint& value) {
   if (value.protocol != fcl::transport::endpoint::protocol_kind::quic_v1) {
      throw_invalid_transport_endpoint(value, "QUIC transport listener requires quic_v1 endpoint protocol");
   }
   if (value.host.empty()) {
      throw_invalid_transport_endpoint(value, "QUIC transport listener requires non-empty host");
   }
   switch (value.host_type) {
   case fcl::transport::endpoint::host_kind::ip4:
   case fcl::transport::endpoint::host_kind::ip6:
      return endpoint{.host = value.host, .port = value.port};
   case fcl::transport::endpoint::host_kind::dns:
   case fcl::transport::endpoint::host_kind::dns4:
   case fcl::transport::endpoint::host_kind::dns6:
      throw_invalid_transport_endpoint(value, "QUIC transport listener cannot bind DNS host kind");
   }
   throw_invalid_transport_endpoint(value, "QUIC transport listener received unsupported host kind");
}

[[nodiscard]] client_options apply_limits(client_options value, const fcl::transport::connect_options& options) {
   if (has_custom_limits(options.limits)) {
      value.limits = from_transport_limits(options.limits);
   }
   return value;
}

[[nodiscard]] server_options apply_limits(server_options value, const fcl::transport::listen_options& options) {
   if (has_custom_limits(options.limits)) {
      value.limits = from_transport_limits(options.limits);
   }
   return value;
}

class quic_session_connector_concept final : public fcl::transport::detail::session_connector_concept {
 public:
   quic_session_connector_concept(fcl::asio::runtime& runtime, client_options options)
       : connector_(runtime), options_(std::move(options)) {}

   [[nodiscard]] bool valid() const noexcept override {
      return active_.load(std::memory_order_acquire);
   }

   boost::asio::awaitable<fcl::transport::session_connection>
   async_connect(fcl::transport::endpoint remote, fcl::transport::connect_options options) override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::canceled, "QUIC transport connector canceled");
      }
      const auto requested = validate_connect_endpoint(remote);
      auto connection = co_await connector_.async_connect(requested, apply_limits(options_, options));
      co_return fcl::transport::session_connection{
          .local_endpoint = to_transport_endpoint(connection.local_endpoint()),
          .remote_endpoint = to_transport_endpoint(connection.remote_endpoint()),
          .session = as_transport_session(std::move(connection)),
      };
   }

   void cancel() override {
      active_.store(false, std::memory_order_release);
      connector_.cancel();
   }

 private:
   connector connector_;
   client_options options_;
   std::atomic_bool active_ = true;
};

class quic_session_listener_concept final : public fcl::transport::detail::session_listener_concept {
 public:
   quic_session_listener_concept(fcl::asio::runtime& runtime, fcl::transport::endpoint local, server_options options,
                                 fcl::transport::listen_options listen_options)
       : listener_(std::make_unique<listener>(runtime, validate_listen_endpoint(local),
                                              apply_limits(std::move(options), listen_options))) {}

   [[nodiscard]] bool valid() const noexcept override {
      return active_.load(std::memory_order_acquire) && listener_ != nullptr;
   }

   [[nodiscard]] fcl::transport::endpoint local_endpoint() const override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::connection_closed, "invalid QUIC transport listener");
      }
      return to_transport_endpoint(listener_->local_endpoint());
   }

   boost::asio::awaitable<fcl::transport::session_connection> async_accept() override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::connection_closed, "invalid QUIC transport listener");
      }
      auto connection = co_await listener_->async_accept();
      co_return fcl::transport::session_connection{
          .local_endpoint = to_transport_endpoint(connection.local_endpoint()),
          .remote_endpoint = to_transport_endpoint(connection.remote_endpoint()),
          .session = as_transport_session(std::move(connection)),
      };
   }

   boost::asio::awaitable<void> async_close() override {
      active_.store(false, std::memory_order_release);
      if (listener_) {
         listener_->stop();
      }
      co_return;
   }

   void cancel() override {
      active_.store(false, std::memory_order_release);
      if (listener_) {
         listener_->stop();
      }
   }

 private:
   std::unique_ptr<listener> listener_;
   std::atomic_bool active_ = true;
};

} // namespace

fcl::transport::limits to_transport_limits(const transport_limits& value) {
   return fcl::transport::limits{
       .max_connections = value.max_connections,
       .max_streams_per_connection = value.max_streams_per_connection,
       .max_queued_bytes = value.max_queued_bytes,
       .max_inbound_queued_bytes = value.max_inbound_queued_bytes,
       .max_inbound_queued_packets = value.max_inbound_queued_packets,
       .max_frame_size = value.max_frame_size,
   };
}

transport_limits from_transport_limits(const fcl::transport::limits& value) {
   return transport_limits{
       .max_connections = value.max_connections,
       .max_streams_per_connection = value.max_streams_per_connection,
       .max_queued_bytes = value.max_queued_bytes,
       .max_inbound_queued_bytes = value.max_inbound_queued_bytes,
       .max_inbound_queued_packets = value.max_inbound_queued_packets,
       .max_frame_size = value.max_frame_size,
   };
}

[[nodiscard]] fcl::transport::endpoint::host_kind host_kind_for(std::string_view host) {
   auto error = boost::system::error_code{};
   const auto address = boost::asio::ip::make_address(host, error);
   if (error) {
      return fcl::transport::endpoint::host_kind::dns;
   }
   if (address.is_v4()) {
      return fcl::transport::endpoint::host_kind::ip4;
   }
   return fcl::transport::endpoint::host_kind::ip6;
}

fcl::transport::endpoint to_transport_endpoint(const endpoint& value) {
   return fcl::transport::endpoint{
       .host_type = host_kind_for(value.host),
       .protocol = fcl::transport::endpoint::protocol_kind::quic_v1,
       .host = value.host,
       .port = value.port,
   };
}

endpoint from_transport_endpoint(const fcl::transport::endpoint& value) {
   if (value.protocol != fcl::transport::endpoint::protocol_kind::quic_v1) {
      FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "transport endpoint is not QUIC");
   }
   return endpoint{.host = value.host, .port = value.port};
}

fcl::transport::stream as_transport_stream(stream value) {
   return fcl::transport::detail::stream_access::make(std::make_shared<quic_stream_concept>(std::move(value)));
}

fcl::transport::session as_transport_session(connection value) {
   return fcl::transport::detail::session_access::make(std::make_shared<quic_session_concept>(std::move(value)));
}

fcl::transport::session_connector make_session_connector(fcl::asio::runtime& runtime, client_options options) {
   return fcl::transport::detail::session_connector_access::make(
       std::make_shared<quic_session_connector_concept>(runtime, std::move(options)));
}

fcl::transport::session_listener make_session_listener(fcl::asio::runtime& runtime, fcl::transport::endpoint local,
                                                       server_options options,
                                                       fcl::transport::listen_options listen_options) {
   return fcl::transport::detail::session_listener_access::make(std::make_shared<quic_session_listener_concept>(
       runtime, std::move(local), std::move(options), listen_options));
}

void register_session(fcl::transport::registry& registry, fcl::asio::runtime& runtime, client_options client,
                      server_options server) {
   registry.register_session(
       fcl::transport::endpoint::protocol_kind::quic_v1,
       [&runtime, client] {
          return make_session_connector(runtime, client);
       },
       [&runtime, server](fcl::transport::endpoint local,
                          fcl::transport::listen_options options) -> boost::asio::awaitable<fcl::transport::session_listener> {
          co_return make_session_listener(runtime, std::move(local), server, options);
       });
}

} // namespace fcl::quic
