#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

#include <concepts>
#include <memory>
#include <string>
#include <utility>

import forge.api.core.bidirectional_stream_call;
import forge.api.core.call_options;
import forge.api.core.client_stream_call;
import forge.api.core.duplex_stream;
import forge.api.core.exceptions;
import forge.api.core.descriptor;
import forge.api.core.handle;
import forge.api.core.binding;
import forge.api.core.registry;
import forge.api.core.server_stream_call;
import forge.api.core.stream_reader;
import forge.api.core.stream_writer;
import forge.api.core.types;

using package_writer = forge::api::core::stream_writer<std::string>;
using package_reader = forge::api::core::stream_reader<std::string>;
using package_duplex =
   forge::api::core::duplex_stream<std::string, std::string>;
using package_server_call =
   forge::api::core::server_stream_call<std::string>;
using package_client_call =
   forge::api::core::client_stream_call<std::string, std::string>;
using package_bidirectional_call =
   forge::api::core::bidirectional_stream_call<std::string, std::string>;

static_assert(std::movable<package_writer> &&
              !std::copy_constructible<package_writer>);
static_assert(std::movable<package_reader> &&
              !std::copy_constructible<package_reader>);
static_assert(std::movable<package_duplex> &&
              !std::copy_constructible<package_duplex>);
static_assert(std::movable<package_server_call> &&
              !std::copy_constructible<package_server_call>);
static_assert(std::movable<package_client_call> &&
              !std::copy_constructible<package_client_call>);
static_assert(std::movable<package_bidirectional_call> &&
              !std::copy_constructible<package_bidirectional_call>);

class local_request {
 public:
   explicit local_request(std::string value) : value_(std::make_unique<std::string>(std::move(value))) {}
   local_request(local_request&&) noexcept = default;
   local_request(const local_request&) = delete;

 private:
   std::unique_ptr<std::string> value_;
};

class local_response {
 public:
   explicit local_response(std::string value) : value_(std::make_unique<std::string>(std::move(value))) {}
   local_response(local_response&&) noexcept = default;
   local_response(const local_response&) = delete;

 private:
   std::unique_ptr<std::string> value_;
};

class local_api : public forge::api::core::contract<local_api> {
 public:
   virtual ~local_api() = default;
   virtual boost::asio::awaitable<local_response> transform(local_request request) = 0;
};

FORGE_API(local_api, FORGE_API_CONTRACT("package.local", 1, 0), FORGE_API_METHOD(transform))

int main() {
   auto registry = forge::api::core::registry{};
   const auto plan = std::move(forge::api::core::binding().serve(registry)).build();
   const auto stream_limits = forge::api::core::call_options{
      .max_item_bytes = 4096,
      .max_buffered_items = 2,
      .max_buffered_bytes = 8192,
   };
   const auto available = forge::api::core::descriptor{
       .id = {.value = "package.smoke"},
       .version = {.major = 1, .revision = 2},
   };
   const auto requested = forge::api::core::api_ref{
       .id = {.value = "package.smoke"},
       .major = 1,
       .min_revision = 2,
   };
   const auto local = local_api::describe();
   const auto* transform = forge::api::core::find_method(local, "transform");
   return forge::api::core::compatible(available, requested) && plan.local == &registry &&
                  transform != nullptr && !transform->raw_invoker &&
                  stream_limits.max_buffered_items == 2
              ? 0
              : 1;
}
