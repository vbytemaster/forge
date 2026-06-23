module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.quic.connector;

import forge.asio.runtime;
import forge.quic.endpoint;
import forge.quic.options;
export import forge.quic.connection;

export namespace forge::quic {

class connector {
 public:
   explicit connector(forge::asio::runtime& runtime);
   ~connector();

   connector(const connector&) = delete;
   connector& operator=(const connector&) = delete;

   boost::asio::awaitable<connection> async_connect(endpoint remote, client_options options = {});
   void cancel();

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::quic
