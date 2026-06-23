module;

#include <memory>
#include <optional>

#include <boost/asio/awaitable.hpp>

export module forge.quic.connection;

import forge.quic.metrics;
import forge.quic.security;
export import forge.quic.endpoint;
export import forge.quic.stream;

export namespace forge::quic {

namespace detail {
struct connection_handle;
struct connection_access;
} // namespace detail

class connection {
 public:
   connection();
   ~connection();

   connection(connection&&) noexcept;
   connection& operator=(connection&&) noexcept;

   connection(const connection&) = delete;
   connection& operator=(const connection&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] endpoint local_endpoint() const;
   [[nodiscard]] endpoint remote_endpoint() const;
   [[nodiscard]] connection_metrics metrics() const;
   [[nodiscard]] std::optional<peer_certificate> peer_certificate() const;

   boost::asio::awaitable<stream> async_open_stream();
   boost::asio::awaitable<stream> async_accept_stream();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend struct detail::connection_access;

   explicit connection(detail::connection_handle handle);

   struct impl;
   std::shared_ptr<impl> impl_;
};

namespace detail {

struct connection_access {
   static connection make(connection_handle handle);
};

} // namespace detail

} // namespace forge::quic
