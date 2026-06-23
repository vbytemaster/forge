module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.quic.listener;

import forge.asio.runtime;
import forge.quic.endpoint;
import forge.quic.options;
export import forge.quic.connection;

export namespace forge::quic {

class listener {
 public:
   listener(forge::asio::runtime& runtime, endpoint bind_endpoint, server_options options);
   ~listener();

   listener(const listener&) = delete;
   listener& operator=(const listener&) = delete;

   [[nodiscard]] endpoint local_endpoint() const;
   boost::asio::awaitable<connection> async_accept();
   void stop();

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::quic
