module;

#include <memory>

#include <boost/asio/awaitable.hpp>

export module fcl.quic.connector;

import fcl.asio.runtime;
import fcl.quic.endpoint;
import fcl.quic.options;
export import fcl.quic.connection;

export namespace fcl::quic {

class connector {
 public:
   explicit connector(fcl::asio::runtime& runtime);
   ~connector();

   connector(const connector&) = delete;
   connector& operator=(const connector&) = delete;

   boost::asio::awaitable<connection> async_connect(endpoint remote, client_options options = {});
   void cancel();

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace fcl::quic
