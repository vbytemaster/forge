#pragma once

namespace fcl::plugins::http_server {

struct pending_api_binding {
   fcl::http::api_binding binding;
   publish_options options;
};

struct startup_snapshot {
   std::vector<pending_api_binding> api_bindings;
   std::vector<middleware_descriptor> middleware;
};

struct plugin::impl {
   mutable std::mutex mutex;
   config settings;
   fcl::asio::runtime* runtime = nullptr;
   const fcl::api::registry* apis = nullptr;
   std::vector<pending_api_binding> api_bindings;
   std::vector<middleware_descriptor> middleware;
   std::unique_ptr<fcl::http::server> server;
   bool publication_closed = false;
   bool stopping = false;

   void add(pending_api_binding value);
   void add(middleware_descriptor value);
   [[nodiscard]] startup_snapshot close_publication();
   void reset_runtime() noexcept;
};

} // namespace fcl::plugins::http_server
