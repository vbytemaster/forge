module;

#include <fcl/exception/macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

module fcl.quic.transport;

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

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      co_return co_await value_.async_read();
   }

   boost::asio::awaitable<void> async_close() override {
      co_await value_.async_close();
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

} // namespace

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

} // namespace fcl::quic
