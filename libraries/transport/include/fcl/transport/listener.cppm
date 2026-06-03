module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module fcl.transport.listener;

export import fcl.transport.connector;

export namespace fcl::transport {

struct listen_options {
   limits limits{};
};

namespace detail {
class stream_listener_concept;
class session_listener_concept;
struct stream_listener_access;
struct session_listener_access;
} // namespace detail

class stream_listener {
 public:
   stream_listener();
   ~stream_listener();

   stream_listener(stream_listener&&) noexcept;
   stream_listener& operator=(stream_listener&&) noexcept;

   stream_listener(const stream_listener&) = delete;
   stream_listener& operator=(const stream_listener&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] endpoint local_endpoint() const;

   boost::asio::awaitable<stream_connection> async_accept();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend struct detail::stream_listener_access;

   struct impl;
   explicit stream_listener(std::shared_ptr<detail::stream_listener_concept> model);

   std::shared_ptr<impl> impl_;
};

class session_listener {
 public:
   session_listener();
   ~session_listener();

   session_listener(session_listener&&) noexcept;
   session_listener& operator=(session_listener&&) noexcept;

   session_listener(const session_listener&) = delete;
   session_listener& operator=(const session_listener&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] endpoint local_endpoint() const;

   boost::asio::awaitable<session_connection> async_accept();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend struct detail::session_listener_access;

   struct impl;
   explicit session_listener(std::shared_ptr<detail::session_listener_concept> model);

   std::shared_ptr<impl> impl_;
};

namespace detail {

class stream_listener_concept {
 public:
   virtual ~stream_listener_concept() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   [[nodiscard]] virtual endpoint local_endpoint() const = 0;
   virtual boost::asio::awaitable<stream_connection> async_accept() = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
   virtual void cancel() = 0;
};

class session_listener_concept {
 public:
   virtual ~session_listener_concept() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   [[nodiscard]] virtual endpoint local_endpoint() const = 0;
   virtual boost::asio::awaitable<session_connection> async_accept() = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
   virtual void cancel() = 0;
};

struct stream_listener_access {
   [[nodiscard]] static stream_listener make(std::shared_ptr<stream_listener_concept> model);
};

struct session_listener_access {
   [[nodiscard]] static session_listener make(std::shared_ptr<session_listener_concept> model);
};

} // namespace detail

} // namespace fcl::transport
