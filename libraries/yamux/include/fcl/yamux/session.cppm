module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module fcl.yamux.session;

export import fcl.transport.session;
export import fcl.yamux.exceptions;
export import fcl.yamux.options;

export namespace fcl::yamux {

class session {
 public:
   session();
   session(transport::stream stream, side session_side, options session_options = {});
   ~session();

   session(session&&) noexcept;
   session& operator=(session&&) noexcept;

   session(const session&) = delete;
   session& operator=(const session&) = delete;

   [[nodiscard]] bool valid() const noexcept;

   boost::asio::awaitable<transport::stream> async_open_stream();
   boost::asio::awaitable<transport::stream> async_accept_stream();
   boost::asio::awaitable<void> async_close();
   void cancel();

   [[nodiscard]] transport::session as_transport() &&;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] transport::session make_session(transport::stream stream, side session_side,
                                              options session_options = {});

} // namespace fcl::yamux
