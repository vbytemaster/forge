#pragma once

namespace fcl::plugins::http_server::detail {

class server_state {
 public:
   explicit server_state(config settings);
   ~server_state();

   server_state(const server_state&) = delete;
   server_state& operator=(const server_state&) = delete;

   void set_runtime(fcl::asio::runtime& runtime) noexcept;
   boost::asio::awaitable<void> start(publication_snapshot snapshot);
   void request_stop() noexcept;
   boost::asio::awaitable<void> stop();

 private:
   config settings_;
   fcl::asio::runtime* runtime_ = nullptr;
   std::unique_ptr<fcl::http::server> server_;
};

} // namespace fcl::plugins::http_server::detail
