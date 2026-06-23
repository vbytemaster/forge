module;

#include <functional>
#include <memory>
#include <cstdint>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

export module forge.websocket.connection;

export namespace forge::websocket {

struct connection_metrics {
   std::uint64_t sent_messages = 0;
   std::uint64_t received_messages = 0;
   std::uint64_t failed_writes = 0;
   std::uint64_t ping_count = 0;
   std::uint64_t close_count = 0;
   std::uint64_t handler_failures = 0;
   std::size_t queued_writes = 0;
};

class connection final : public std::enable_shared_from_this<connection> {
 public:
   using ptr = std::shared_ptr<connection>;
   using message_handler = std::function<boost::asio::awaitable<void>(connection&, std::string)>;
   using close_handler = std::function<void(connection&)>;

   ~connection();

   connection(const connection&) = delete;
   connection& operator=(const connection&) = delete;

   void on_message(message_handler handler);
   void on_close(close_handler handler);

   boost::asio::awaitable<void> send(std::string message);
   boost::asio::awaitable<void> close();
   boost::asio::awaitable<void> ping(std::string payload = {});
   [[nodiscard]] connection_metrics metrics() const;

   static ptr create(boost::beast::tcp_stream stream);
   static ptr create(boost::beast::ssl_stream<boost::beast::tcp_stream> stream);
   boost::asio::awaitable<void> accept(const boost::beast::http::request<boost::beast::http::string_body>& request);
   boost::asio::awaitable<void> handshake(std::string host, std::string target);
   void start_read_loop();

 private:
   struct impl;

   explicit connection(boost::beast::tcp_stream stream);
   explicit connection(boost::beast::ssl_stream<boost::beast::tcp_stream> stream);

   std::unique_ptr<impl> impl_;
};

} // namespace forge::websocket
