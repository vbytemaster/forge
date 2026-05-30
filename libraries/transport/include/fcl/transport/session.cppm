module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module fcl.transport.session;

export import fcl.transport.stream;

export namespace fcl::transport {

namespace detail {
class session_concept;
struct session_access;
} // namespace detail

class session {
 public:
   session();
   ~session();

   session(session&&) noexcept;
   session& operator=(session&&) noexcept;

   session(const session&) = delete;
   session& operator=(const session&) = delete;

   [[nodiscard]] bool valid() const noexcept;

   boost::asio::awaitable<stream> async_open_stream();
   boost::asio::awaitable<stream> async_accept_stream();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend struct detail::session_access;

   struct impl;
   explicit session(std::shared_ptr<detail::session_concept> model);

   std::shared_ptr<impl> impl_;
};

namespace detail {

class session_concept {
 public:
   virtual ~session_concept() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   virtual boost::asio::awaitable<stream> async_open_stream() = 0;
   virtual boost::asio::awaitable<stream> async_accept_stream() = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
   virtual void cancel() = 0;
};

struct session_access {
   [[nodiscard]] static session make(std::shared_ptr<session_concept> model);
};

} // namespace detail

} // namespace fcl::transport
