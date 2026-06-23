module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.yamux.session;

export import forge.transport.session;
export import forge.yamux.exceptions;
export import forge.yamux.options;

export namespace forge::yamux {

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

} // namespace forge::yamux
