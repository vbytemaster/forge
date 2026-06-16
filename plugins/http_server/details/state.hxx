#pragma once

namespace fcl::plugins::http_server {

struct publication_snapshot {
   std::vector<publication> publications;
   std::vector<fcl::http::middleware_descriptor> middleware;
};

struct plugin::impl {
   mutable std::mutex mutex;
   config settings;
   fcl::asio::runtime* runtime = nullptr;
   const fcl::api::registry* apis = nullptr;
   std::vector<publication> publications;
   std::vector<fcl::http::middleware_descriptor> middleware;
   std::unique_ptr<fcl::http::server> server;
   bool publication_closed = false;
   bool stopping = false;

   void add(publication value);
   void add(fcl::http::middleware_descriptor value);
   [[nodiscard]] publication_snapshot close_publication();
   void reset_runtime() noexcept;
};

} // namespace fcl::plugins::http_server
