module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.transport.connector;

export import forge.transport.endpoint;
export import forge.transport.limits;
export import forge.transport.session;
export import forge.transport.stream;

export namespace forge::transport {

struct connect_options {
   limits limits{};
};

struct stream_connection {
   endpoint local_endpoint;
   endpoint remote_endpoint;
   stream stream;
};

struct session_connection {
   endpoint local_endpoint;
   endpoint remote_endpoint;
   session session;
};

namespace detail {
class stream_connector_concept;
class session_connector_concept;
struct stream_connector_access;
struct session_connector_access;
} // namespace detail

class stream_connector {
 public:
   stream_connector();
   ~stream_connector();

   stream_connector(stream_connector&&) noexcept;
   stream_connector& operator=(stream_connector&&) noexcept;

   stream_connector(const stream_connector&) = delete;
   stream_connector& operator=(const stream_connector&) = delete;

   [[nodiscard]] bool valid() const noexcept;

   boost::asio::awaitable<stream_connection> async_connect(endpoint remote, connect_options options = {});
   void cancel();

 private:
   friend struct detail::stream_connector_access;

   struct impl;
   explicit stream_connector(std::shared_ptr<detail::stream_connector_concept> model);

   std::shared_ptr<impl> impl_;
};

class session_connector {
 public:
   session_connector();
   ~session_connector();

   session_connector(session_connector&&) noexcept;
   session_connector& operator=(session_connector&&) noexcept;

   session_connector(const session_connector&) = delete;
   session_connector& operator=(const session_connector&) = delete;

   [[nodiscard]] bool valid() const noexcept;

   boost::asio::awaitable<session_connection> async_connect(endpoint remote, connect_options options = {});
   void cancel();

 private:
   friend struct detail::session_connector_access;

   struct impl;
   explicit session_connector(std::shared_ptr<detail::session_connector_concept> model);

   std::shared_ptr<impl> impl_;
};

namespace detail {

class stream_connector_concept {
 public:
   virtual ~stream_connector_concept() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   virtual boost::asio::awaitable<stream_connection> async_connect(endpoint remote, connect_options options) = 0;
   virtual void cancel() = 0;
};

class session_connector_concept {
 public:
   virtual ~session_connector_concept() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   virtual boost::asio::awaitable<session_connection> async_connect(endpoint remote, connect_options options) = 0;
   virtual void cancel() = 0;
};

struct stream_connector_access {
   [[nodiscard]] static stream_connector make(std::shared_ptr<stream_connector_concept> model);
};

struct session_connector_access {
   [[nodiscard]] static session_connector make(std::shared_ptr<session_connector_concept> model);
};

} // namespace detail

} // namespace forge::transport
