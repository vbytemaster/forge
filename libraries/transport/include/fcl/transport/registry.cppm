module;

#include <functional>
#include <memory>

#include <boost/asio/awaitable.hpp>

export module fcl.transport.registry;

export import fcl.transport.listener;

export namespace fcl::transport {

using stream_connector_factory = std::function<stream_connector()>;
using session_connector_factory = std::function<session_connector()>;
using stream_listener_factory = std::function<boost::asio::awaitable<stream_listener>(endpoint local, listen_options options)>;
using session_listener_factory = std::function<boost::asio::awaitable<session_listener>(endpoint local, listen_options options)>;

class registry {
 public:
   registry();
   ~registry();

   registry(registry&&) noexcept;
   registry& operator=(registry&&) noexcept;

   registry(const registry&) = delete;
   registry& operator=(const registry&) = delete;

   void register_stream(endpoint::protocol_kind protocol, stream_connector_factory connector, stream_listener_factory listener);
   void register_session(endpoint::protocol_kind protocol, session_connector_factory connector,
                         session_listener_factory listener);

   [[nodiscard]] bool has_stream(endpoint::protocol_kind protocol) const;
   [[nodiscard]] bool has_session(endpoint::protocol_kind protocol) const;

   boost::asio::awaitable<stream_connection> async_connect_stream(endpoint remote, connect_options options = {});
   boost::asio::awaitable<session_connection> async_connect_session(endpoint remote, connect_options options = {});
   boost::asio::awaitable<stream_listener> async_listen_stream(endpoint local, listen_options options = {});
   boost::asio::awaitable<session_listener> async_listen_session(endpoint local, listen_options options = {});

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::transport
