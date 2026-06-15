module;

#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.plugins.http_server.plugin;

import fcl.asio.runtime;
import fcl.exceptions;
import fcl.http.api;
import fcl.http.exceptions;
import fcl.http.file;
import fcl.http.middleware;
import fcl.http.router;
import fcl.http.server;
import fcl.http.stream;
import fcl.http.types;
import fcl.http.upload;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.file_publisher;
import fcl.plugins.http_server.types;
import fcl.plugins.http_server.upload_publisher;

#include "private/base_path.hxx"
#include "private/publication_store.hxx"
#include "private/server_state.hxx"

namespace fcl::plugins::http_server::detail {
namespace {

std::string selected_base_path(const publish_options& options, const config& settings) {
   return options.base_path.empty() ? settings.api_base_path : options.base_path;
}

std::string mount_path(const std::string& base_path, const std::string& route_path) {
   if (base_path.empty() || base_path == "/") {
      return route_path;
   }
   return base_path + route_path;
}

void mount_file_publication(fcl::http::router& router, const config& settings, file_publication publication,
                            publish_options options) {
   const auto route = mount_path(selected_base_path(options, settings), publication.route_path);
   auto root = std::make_shared<fcl::http::static_file_root>(std::move(publication.root),
                                                             std::move(publication.options));
   auto path_parameter = std::move(publication.path_parameter);
   auto handler = [root, path_parameter](fcl::http::stream_request& request)
                    -> boost::asio::awaitable<fcl::http::stream_response> {
      const auto relative_path = request.context.route_param(path_parameter);
      if (!relative_path.has_value()) {
         co_return fcl::http::stream_response::buffered(
            fcl::http::make_text_response(request.context.request, fcl::http::status::bad_request,
                                          "missing file path"));
      }
      co_return co_await root->serve(request, *relative_path);
   };
   router.get_stream(route, handler);
   router.head_stream(route, std::move(handler));
}

void mount_upload_publication(fcl::http::router& router, const config& settings, upload_publication publication,
                              publish_options options) {
   const auto route = mount_path(selected_base_path(options, settings), publication.route_path);
   auto upload_options = std::move(publication.options);
   auto handler = std::move(publication.handler);
   auto route_handler = [upload_options = std::move(upload_options), handler = std::move(handler)](
                           fcl::http::stream_request& request) mutable
      -> boost::asio::awaitable<fcl::http::stream_response> {
      auto upload = upload_request{.context = request.context,
                                   .upload = fcl::http::upload_reader{std::move(request.body), upload_options}};
      co_return co_await handler(upload);
   };

   if (publication.method == fcl::http::method::post) {
      router.post_stream(route, std::move(route_handler));
   } else if (publication.method == fcl::http::method::put) {
      router.put_stream(route, std::move(route_handler));
   } else {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP upload publication supports POST or PUT only");
   }
}

} // namespace

server_state::server_state(config settings) : settings_{std::move(settings)} {}

server_state::~server_state() = default;

void server_state::set_runtime(fcl::asio::runtime& runtime) noexcept {
   runtime_ = &runtime;
}

boost::asio::awaitable<void> server_state::start(publication_snapshot snapshot) {
   if (runtime_ == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "HTTP server plugin is not initialized");
   }
   if (server_) {
      co_return;
   }

   auto router = fcl::http::router{};
   try {
      for (auto& descriptor : snapshot.middleware) {
         router.use(std::move(descriptor));
      }
      for (auto& publication : snapshot.publications) {
         const auto base_path = publication.options.base_path.empty() ? settings_.api_base_path
                                                                      : publication.options.base_path;
         publication.binding.mount(router, base_path == "/" ? std::string_view{} : std::string_view{base_path});
      }
      for (auto& [publication, options] : snapshot.files) {
         mount_file_publication(router, settings_, std::move(publication), std::move(options));
      }
      for (auto& [publication, options] : snapshot.uploads) {
         mount_upload_publication(router, settings_, std::move(publication), std::move(options));
      }
   } catch (const fcl::http::exceptions::conflict& error) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict, error.message());
   }

   auto server_config = fcl::http::server_config{
      .bind_address = settings_.bind_address,
      .port = static_cast<std::uint16_t>(settings_.port),
      .max_request_body_bytes = settings_.max_request_body_bytes,
      .max_header_bytes = settings_.max_header_bytes,
      .read_timeout = std::chrono::milliseconds{settings_.read_timeout_ms},
      .idle_timeout = std::chrono::milliseconds{settings_.idle_timeout_ms},
   };
   server_ = std::make_unique<fcl::http::server>(*runtime_, std::move(server_config), std::move(router));
   co_await server_->async_start();
}

void server_state::request_stop() noexcept {
   if (server_) {
      server_->stop();
   }
}

boost::asio::awaitable<void> server_state::stop() {
   if (server_) {
      co_await server_->async_stop();
      server_.reset();
   }
}

} // namespace fcl::plugins::http_server::detail
