#pragma once

namespace forge::plugins::http::server {

struct pending_binding {
   forge::http::api::binding_plan binding;
   publish_options options;
};

struct startup_snapshot {
   std::vector<pending_binding> bindings;
   std::vector<middleware_descriptor> middleware;
};

struct plugin::impl {
   mutable std::mutex mutex;
   config settings;
   forge::asio::runtime* runtime = nullptr;
   const forge::api::registry* apis = nullptr;
   std::vector<pending_binding> bindings;
   std::vector<middleware_descriptor> middleware;
   std::unique_ptr<forge::http::server> server;
   bool publication_closed = false;
   bool stopping = false;

   void add(pending_binding value);
   void add(middleware_descriptor value);
   [[nodiscard]] startup_snapshot close_publication();
   void reset_runtime() noexcept;
};

} // namespace forge::plugins::http::server
