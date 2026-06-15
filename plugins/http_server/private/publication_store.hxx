#pragma once

namespace fcl::plugins::http_server::detail {

struct publication_entry {
   fcl::http::api_binding binding;
   publish_options options;
};

struct publication_snapshot {
   std::vector<publication_entry> publications;
   std::vector<fcl::http::middleware_descriptor> middleware;
};

class publication_store {
 public:
   void publish(fcl::http::api_binding binding, publish_options options);
   void use(fcl::http::middleware_descriptor descriptor);
   [[nodiscard]] publication_snapshot close();

 private:
   bool closed_ = false;
   std::vector<publication_entry> publications_;
   std::vector<fcl::http::middleware_descriptor> middleware_;
};

} // namespace fcl::plugins::http_server::detail
