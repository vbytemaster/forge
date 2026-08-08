#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <typeindex>
#include <tuple>
#include <vector>

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.reflect.reflect;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace cache_errors {

enum class code : std::uint8_t {
   chunk_not_found = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "test.cache")

using chunk_not_found = forge::exceptions::coded_exception<code, code::chunk_not_found>;

} // namespace cache_errors

namespace protocol {

struct read_chunk {
   std::string ref;
};

struct read_old_request {
   std::string ref;
};

struct chunk {
   std::string bytes;
};

} // namespace protocol

BOOST_DESCRIBE_STRUCT(protocol::read_chunk, (), (ref))
BOOST_DESCRIBE_STRUCT(protocol::read_old_request, (), (ref))
BOOST_DESCRIBE_STRUCT(protocol::chunk, (), (bytes))

namespace protocol {

template <typename Stream> Stream& operator<<(Stream& stream, const read_chunk& value) {
   forge::raw::pack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, read_chunk& value) {
   forge::raw::unpack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator<<(Stream& stream, const read_old_request& value) {
   forge::raw::pack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, read_old_request& value) {
   forge::raw::unpack(stream, value.ref);
   return stream;
}

template <typename Stream> Stream& operator<<(Stream& stream, const chunk& value) {
   forge::raw::pack(stream, value.bytes);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, chunk& value) {
   forge::raw::unpack(stream, value.bytes);
   return stream;
}

} // namespace protocol

template <typename T> forge::api::core::bytes pack_api_payload(const T& value) {
   return forge::api::core::pack_body(value);
}

class cache_api : public forge::api::core::contract<cache_api, forge::api::core::surface::local |
                                                                   forge::api::core::surface::remote> {
 public:
   virtual ~cache_api() = default;

   virtual boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) = 0;
   virtual boost::asio::awaitable<protocol::chunk> read_old(protocol::read_old_request request) = 0;
   virtual boost::asio::awaitable<std::vector<protocol::chunk>> watch(protocol::read_chunk request) = 0;
   virtual boost::asio::awaitable<protocol::chunk> upload(std::vector<protocol::read_chunk> requests) = 0;
   virtual boost::asio::awaitable<std::vector<protocol::chunk>> sync(std::vector<protocol::read_chunk> requests) = 0;
};

FORGE_API(cache_api, FORGE_API_CONTRACT("cache", 1, 8), FORGE_API_METHOD(read),
          FORGE_API_METHOD_DEPRECATED(read_old, "use read"), FORGE_API_METHOD_SINCE(watch, 2),
          FORGE_API_METHOD_SINCE(upload, 3), FORGE_API_METHOD_SINCE(sync, 4))

class local_only_api : public forge::api::core::contract<local_only_api> {
 public:
   virtual ~local_only_api() = default;

   [[nodiscard]] virtual std::string name() const = 0;
};

FORGE_API(local_only_api, FORGE_API_CONTRACT("local.only", 1, 0))

class local_request {
 public:
   explicit local_request(std::string value) : value_(std::make_unique<std::string>(std::move(value))) {}
   local_request(local_request&&) noexcept = default;
   local_request& operator=(local_request&&) noexcept = default;
   local_request(const local_request&) = delete;
   local_request& operator=(const local_request&) = delete;

   [[nodiscard]] std::string take() {
      return std::move(*value_);
   }

 private:
   std::unique_ptr<std::string> value_;
};

class local_response {
 public:
   explicit local_response(std::string value) : value_(std::make_unique<std::string>(std::move(value))) {}
   local_response(local_response&&) noexcept = default;
   local_response& operator=(local_response&&) noexcept = default;
   local_response(const local_response&) = delete;
   local_response& operator=(const local_response&) = delete;

   [[nodiscard]] const std::string& value() const noexcept {
      return *value_;
   }

 private:
   std::unique_ptr<std::string> value_;
};

class local_data_api : public forge::api::core::contract<local_data_api> {
 public:
   virtual ~local_data_api() = default;
   virtual boost::asio::awaitable<local_response> transform(local_request request) = 0;
};

FORGE_API(local_data_api, FORGE_API_CONTRACT("local.data", 1, 0), FORGE_API_METHOD(transform))

class remote_only_api : public forge::api::core::contract<remote_only_api, forge::api::core::surface::remote> {
 public:
   virtual ~remote_only_api() = default;

   virtual boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) = 0;
};

FORGE_API(remote_only_api, FORGE_API_CONTRACT("remote.only", 1, 0), FORGE_API_METHOD(read))

class overloaded_api : public forge::api::core::contract<overloaded_api, forge::api::core::surface::remote> {
 public:
   virtual ~overloaded_api() = default;

   virtual boost::asio::awaitable<protocol::chunk> sign(protocol::read_chunk request) = 0;
   virtual boost::asio::awaitable<protocol::chunk> sign_since(protocol::read_chunk request) = 0;
   virtual boost::asio::awaitable<protocol::chunk> sign_old(protocol::read_chunk request) = 0;
   virtual boost::asio::awaitable<protocol::chunk> sign_old_since(protocol::read_chunk request) = 0;

   boost::asio::awaitable<protocol::chunk> sign(std::string ref) {
      co_return co_await sign(protocol::read_chunk{.ref = std::move(ref)});
   }
};

FORGE_API(overloaded_api, FORGE_API_CONTRACT("overloaded", 1, 3),
          FORGE_API_METHOD_TYPED(sign, protocol::read_chunk, protocol::chunk),
          FORGE_API_METHOD_TYPED_SINCE(sign_since, protocol::read_chunk, protocol::chunk, 2),
          FORGE_API_METHOD_TYPED_DEPRECATED(sign_old, protocol::read_chunk, protocol::chunk, "use sign"),
          FORGE_API_METHOD_TYPED_DEPRECATED_SINCE(sign_old_since, protocol::read_chunk, protocol::chunk, 2, "use sign"))

static_assert(forge::api::core::interface<cache_api>);
static_assert(forge::api::core::local_interface<cache_api>);
static_assert(forge::api::core::remote_interface<cache_api>);
static_assert(forge::api::core::supports_surface<cache_api, forge::api::core::surface::local>);
static_assert(forge::api::core::supports_surface<cache_api, forge::api::core::surface::remote>);
static_assert(forge::api::core::interface<local_only_api>);
static_assert(forge::api::core::local_interface<local_only_api>);
static_assert(!forge::api::core::remote_interface<local_only_api>);
static_assert(forge::api::core::local_interface<local_data_api>);
static_assert(!forge::api::core::remote_interface<local_data_api>);
static_assert(forge::api::core::interface<remote_only_api>);
static_assert(!forge::api::core::local_interface<remote_only_api>);
static_assert(forge::api::core::remote_interface<remote_only_api>);
static_assert(forge::api::core::remote_interface<overloaded_api>);

class positional_api : public forge::api::core::contract<positional_api, forge::api::core::surface::local |
                                                                             forge::api::core::surface::remote> {
 public:
   virtual ~positional_api() = default;

   virtual boost::asio::awaitable<protocol::chunk> concat(std::string left, std::string right) = 0;
   virtual boost::asio::awaitable<protocol::chunk> concat_since(std::string left, std::string right) = 0;
   virtual boost::asio::awaitable<protocol::chunk> concat_old(std::string left, std::string right) = 0;
};

FORGE_API(positional_api, FORGE_API_CONTRACT("positional", 1, 2), FORGE_API_METHOD(concat, left, right),
          FORGE_API_METHOD_SINCE(concat_since, 2, left, right),
          FORGE_API_METHOD_DEPRECATED(concat_old, "use concat", left, right))

static_assert(forge::api::core::interface<positional_api>);
static_assert(forge::api::core::local_interface<positional_api>);
static_assert(forge::api::core::remote_interface<positional_api>);

class cache_impl final : public cache_api {
 public:
   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      co_return protocol::chunk{.bytes = std::move(request.ref)};
   }

   boost::asio::awaitable<protocol::chunk> read_old(protocol::read_old_request request) override {
      co_return protocol::chunk{.bytes = std::move(request.ref)};
   }

   boost::asio::awaitable<std::vector<protocol::chunk>> watch(protocol::read_chunk request) override {
      co_return std::vector<protocol::chunk>{
          protocol::chunk{.bytes = request.ref + ":0"},
          protocol::chunk{.bytes = request.ref + ":1"},
      };
   }

   boost::asio::awaitable<protocol::chunk> upload(std::vector<protocol::read_chunk> requests) override {
      auto out = std::string{};
      for (const auto& request : requests) {
         if (!out.empty()) {
            out += ",";
         }
         out += request.ref;
      }
      co_return protocol::chunk{.bytes = std::move(out)};
   }

   boost::asio::awaitable<std::vector<protocol::chunk>> sync(std::vector<protocol::read_chunk> requests) override {
      auto out = std::vector<protocol::chunk>{};
      out.reserve(requests.size());
      for (const auto& request : requests) {
         out.push_back(protocol::chunk{.bytes = request.ref + ":ack"});
      }
      co_return out;
   }
};

class tracking_cache_impl final : public cache_api {
 public:
   explicit tracking_cache_impl(std::shared_ptr<int> upload_calls_value)
       : upload_calls_(std::move(upload_calls_value)) {}

   tracking_cache_impl(std::shared_ptr<int> upload_calls_value, std::shared_ptr<int> watch_calls_value)
       : upload_calls_(std::move(upload_calls_value)), watch_calls_(std::move(watch_calls_value)) {}

   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk request) override {
      co_return protocol::chunk{.bytes = std::move(request.ref)};
   }

   boost::asio::awaitable<protocol::chunk> read_old(protocol::read_old_request request) override {
      co_return protocol::chunk{.bytes = std::move(request.ref)};
   }

   boost::asio::awaitable<std::vector<protocol::chunk>> watch(protocol::read_chunk request) override {
      if (watch_calls_) {
         ++*watch_calls_;
      }
      co_return std::vector<protocol::chunk>{protocol::chunk{.bytes = request.ref}};
   }

   boost::asio::awaitable<protocol::chunk> upload(std::vector<protocol::read_chunk> requests) override {
      ++*upload_calls_;
      auto out = std::string{};
      for (const auto& request : requests) {
         if (!out.empty()) {
            out += ",";
         }
         out += request.ref;
      }
      co_return protocol::chunk{.bytes = std::move(out)};
   }

   boost::asio::awaitable<std::vector<protocol::chunk>> sync(std::vector<protocol::read_chunk> requests) override {
      auto out = std::vector<protocol::chunk>{};
      out.reserve(requests.size());
      for (const auto& request : requests) {
         out.push_back(protocol::chunk{.bytes = request.ref});
      }
      co_return out;
   }

 private:
   std::shared_ptr<int> upload_calls_;
   std::shared_ptr<int> watch_calls_;
};

class positional_impl final : public positional_api {
 public:
   boost::asio::awaitable<protocol::chunk> concat(std::string left, std::string right) override {
      co_return protocol::chunk{.bytes = std::move(left) + ":" + std::move(right)};
   }

   boost::asio::awaitable<protocol::chunk> concat_since(std::string left, std::string right) override {
      co_return protocol::chunk{.bytes = std::move(left) + ":since:" + std::move(right)};
   }

   boost::asio::awaitable<protocol::chunk> concat_old(std::string left, std::string right) override {
      co_return protocol::chunk{.bytes = std::move(left) + ":old:" + std::move(right)};
   }
};

class local_data_impl final : public local_data_api {
 public:
   boost::asio::awaitable<local_response> transform(local_request request) override {
      co_return local_response{"local:" + request.take()};
   }
};

void build_empty_id_descriptor() {
   (void)forge::api::core::define<cache_api>({.id = {""}, .version = {.major = 1, .revision = 0}}).build();
}

void build_zero_major_descriptor() {
   (void)forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 0, .revision = 0}}).build();
}

void build_duplicate_method_descriptor() {
   (void)forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 0}})
       .method<&cache_api::read, protocol::read_chunk, protocol::chunk>("read")
       .method<&cache_api::read, protocol::read_chunk, protocol::chunk>("read")
       .build();
}

forge::api::core::descriptor cache_descriptor_with_declared_errors() {
   return forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
       .method<&cache_api::read>("read")
       .error<cache_errors::chunk_not_found>("chunk_not_found",
                                             {.status_code = forge::api::core::status::not_found, .retryable = false})
       .build();
}

BOOST_AUTO_TEST_SUITE(api_test_suite)

BOOST_AUTO_TEST_CASE(error_payload_raw_roundtrip) {
   const auto payload = forge::api::core::error_payload{
       .error = "chunk_not_found",
       .message = "chunk not found",
       .retryable = false,
       .identity = {.category = "test.cache", .code = 1},
       .details_codec = forge::api::core::codec_id{"forge.raw"},
       .details = forge::api::core::bytes{'a', 'b', 'c'},
   };

   const auto packed = forge::raw::pack(payload);
   const auto unpacked = forge::raw::unpack<forge::api::core::error_payload>(packed);

   BOOST_CHECK(unpacked == payload);
}

BOOST_AUTO_TEST_CASE(frame_raw_roundtrip) {
   const auto kinds = std::vector<forge::api::core::frame_kind>{
       forge::api::core::frame_kind::request,     forge::api::core::frame_kind::response,
       forge::api::core::frame_kind::error,       forge::api::core::frame_kind::cancel,
       forge::api::core::frame_kind::stream_item, forge::api::core::frame_kind::stream_end,
   };

   for (const auto kind : kinds) {
      const auto frame = forge::api::core::frame{
          .kind = kind,
          .id = {.value = 42},
          .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
          .method = "read",
          .meta = {{.key = "deadline-ms", .value = "5000"}},
          .codec = {.value = "forge.raw"},
          .payload = {'r', 'e', 'q'},
      };

      const auto packed = forge::raw::pack(frame);
      const auto unpacked = forge::raw::unpack<forge::api::core::frame>(packed);

      BOOST_CHECK(unpacked == frame);
   }
}

BOOST_AUTO_TEST_CASE(method_descriptor_records_stream_method_kind) {
   auto descriptor =
       forge::api::core::define<cache_api>({.id = {"cache.streams"}, .version = {.major = 1, .revision = 0}})
           .server_stream<&cache_api::watch, protocol::read_chunk, protocol::chunk>("watch")
           .build();

   const auto* method = forge::api::core::find_method(descriptor, "watch");
   BOOST_REQUIRE(method != nullptr);
   BOOST_CHECK(method->kind == forge::api::core::method_kind::server_stream);
}

class recording_invoker final : public forge::api::core::remote_invoker {
 public:
   boost::asio::awaitable<forge::api::core::response> async_call(forge::api::core::request value) override {
      last = std::move(value);
      co_return forge::api::core::response{
          .api = last.api,
          .method = last.method,
          .codec = last.codec,
          .body = forge::api::core::pack_body(
              protocol::chunk{.bytes = "remote:" + forge::api::core::unpack_body<protocol::read_chunk>(last.body).ref}),
      };
   }

   forge::api::core::request last;
};

class recording_remote_mount final : public forge::api::core::remote_mount {
 public:
   explicit recording_remote_mount(std::shared_ptr<recording_invoker> invoker) : invoker_{std::move(invoker)} {}

 private:
   boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
   open_remote_invoker(forge::api::core::api_ref requested, forge::api::core::descriptor) override {
      last_requested = requested;
      co_return invoker_;
   }

 public:
   forge::api::core::api_ref last_requested;

 private:
   std::shared_ptr<recording_invoker> invoker_;
};

class recording_positional_invoker final : public forge::api::core::remote_invoker {
 public:
   boost::asio::awaitable<forge::api::core::response> async_call(forge::api::core::request value) override {
      last = std::move(value);
      const auto args = forge::api::core::unpack_body<std::tuple<std::string, std::string>>(last.body);
      co_return forge::api::core::response{
          .api = last.api,
          .method = last.method,
          .codec = last.codec,
          .body = forge::api::core::pack_body(
              protocol::chunk{.bytes = "remote:" + std::get<0>(args) + ":" + std::get<1>(args)}),
      };
   }

   forge::api::core::request last;
};

BOOST_AUTO_TEST_CASE(generated_api_descriptor_records_contract_and_method_metadata) {
   const auto descriptor = cache_api::describe();

   BOOST_TEST(descriptor.id.value == "cache");
   BOOST_TEST(descriptor.version.major == 1U);
   BOOST_TEST(descriptor.version.revision == 8U);
   BOOST_TEST(cache_api::ref().id.value == "cache");
   BOOST_TEST(cache_api::ref().major == 1U);
   BOOST_TEST(cache_api::ref().min_revision == 8U);

   const auto* read = forge::api::core::find_method(descriptor, "read");
   const auto* read_old = forge::api::core::find_method(descriptor, "read_old");
   const auto* watch = forge::api::core::find_method(descriptor, "watch");
   BOOST_REQUIRE(read != nullptr);
   BOOST_REQUIRE(read_old != nullptr);
   BOOST_REQUIRE(watch != nullptr);
   BOOST_TEST(read->since_revision == 0U);
   BOOST_TEST(!read->deprecated);
   BOOST_TEST(read_old->deprecated);
   BOOST_TEST(read_old->deprecation_reason == "use read");
   BOOST_TEST(watch->since_revision == 2U);
}

BOOST_AUTO_TEST_CASE(generated_api_descriptor_supports_typed_overload_methods) {
   const auto descriptor = overloaded_api::describe();

   BOOST_TEST(descriptor.id.value == "overloaded");
   BOOST_TEST(descriptor.version.major == 1U);
   BOOST_TEST(descriptor.version.revision == 3U);

   const auto* sign = forge::api::core::find_method(descriptor, "sign");
   const auto* sign_since = forge::api::core::find_method(descriptor, "sign_since");
   const auto* sign_old = forge::api::core::find_method(descriptor, "sign_old");
   const auto* sign_old_since = forge::api::core::find_method(descriptor, "sign_old_since");
   BOOST_REQUIRE(sign != nullptr);
   BOOST_REQUIRE(sign_since != nullptr);
   BOOST_REQUIRE(sign_old != nullptr);
   BOOST_REQUIRE(sign_old_since != nullptr);
   BOOST_TEST((sign->request_type == std::type_index{typeid(protocol::read_chunk)}));
   BOOST_TEST((sign->response_type == std::type_index{typeid(protocol::chunk)}));
   BOOST_TEST(sign_since->since_revision == 2U);
   BOOST_TEST(sign_old->deprecated);
   BOOST_TEST(sign_old->deprecation_reason == "use sign");
   BOOST_TEST(sign_old_since->since_revision == 2U);
   BOOST_TEST(sign_old_since->deprecated);
   BOOST_TEST(sign_old_since->deprecation_reason == "use sign");
}

BOOST_AUTO_TEST_CASE(generated_api_descriptor_records_positional_argument_names) {
   const auto descriptor = positional_api::describe();

   BOOST_TEST(descriptor.id.value == "positional");
   BOOST_TEST(descriptor.version.major == 1U);
   BOOST_TEST(descriptor.version.revision == 2U);

   const auto* concat = forge::api::core::find_method(descriptor, "concat");
   const auto* concat_since = forge::api::core::find_method(descriptor, "concat_since");
   const auto* concat_old = forge::api::core::find_method(descriptor, "concat_old");
   BOOST_REQUIRE(concat != nullptr);
   BOOST_REQUIRE(concat_since != nullptr);
   BOOST_REQUIRE(concat_old != nullptr);
   BOOST_TEST((concat->request_type == std::type_index{typeid(std::tuple<std::string, std::string>)}));
   BOOST_TEST((concat->response_type == std::type_index{typeid(protocol::chunk)}));
   BOOST_REQUIRE_EQUAL(concat->argument_names.size(), 2U);
   BOOST_TEST(concat->argument_names[0] == "left");
   BOOST_TEST(concat->argument_names[1] == "right");
   BOOST_TEST(concat_since->since_revision == 2U);
   BOOST_CHECK(concat_old->deprecated);
   BOOST_TEST(concat_old->deprecation_reason == "use concat");
}

BOOST_AUTO_TEST_CASE(local_api_does_not_require_raw_serialization) {
   const auto descriptor = local_data_api::describe();
   const auto* transform = forge::api::core::find_method(descriptor, "transform");
   BOOST_REQUIRE(transform != nullptr);
   BOOST_CHECK(forge::api::core::supports(descriptor.supported_surfaces, forge::api::core::surface::local));
   BOOST_CHECK(!forge::api::core::supports(descriptor.supported_surfaces, forge::api::core::surface::remote));
   BOOST_CHECK(!transform->raw_invoker);
   BOOST_TEST((transform->request_type == std::type_index{typeid(local_request)}));
   BOOST_TEST((transform->response_type == std::type_index{typeid(local_response)}));

   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<local_data_api>(std::make_shared<local_data_impl>());
   auto handle = registry.get<local_data_api>(local_data_api::ref());
   const auto response = forge::asio::blocking::run(runtime, handle->transform(local_request{"payload"}));
   BOOST_TEST(response.value() == "local:payload");

   const auto wire_response = forge::asio::blocking::run(runtime, registry.dispatch(forge::api::core::frame{
                                                                      .kind = forge::api::core::frame_kind::request,
                                                                      .api = local_data_api::ref(),
                                                                      .method = "transform",
                                                                      .codec = {.value = "forge.raw"},
                                                                  }));
   BOOST_CHECK(wire_response.kind == forge::api::core::frame_kind::error);
   const auto error = forge::raw::unpack<forge::api::core::error_payload>(wire_response.payload);
   BOOST_CHECK(error.status_code == forge::api::core::status::failed_precondition);
   BOOST_TEST(error.identity.code == static_cast<std::uint32_t>(forge::api::core::exceptions::code::protocol_error));
}

BOOST_AUTO_TEST_CASE(generated_proxy_invokes_remote_through_typed_handle) {
   auto runtime = forge::asio::runtime{};
   auto invoker = std::make_shared<recording_invoker>();
   auto handle = forge::api::core::handle<cache_api>{std::make_shared<forge::api::core::proxy<cache_api>>(invoker)};

   const auto response = forge::asio::blocking::run(runtime, handle->read({.ref = "abc"}));

   BOOST_TEST(response.bytes == "remote:abc");
   BOOST_TEST(invoker->last.api.id.value == "cache");
   BOOST_TEST(invoker->last.api.major == 1U);
   BOOST_TEST(invoker->last.api.min_revision == 8U);
   BOOST_TEST(invoker->last.method == "read");
   BOOST_TEST(invoker->last.codec.value == "forge.raw");
}

BOOST_AUTO_TEST_CASE(generated_proxy_invokes_typed_overload_method) {
   auto runtime = forge::asio::runtime{};
   auto invoker = std::make_shared<recording_invoker>();
   auto handle =
       forge::api::core::handle<overloaded_api>{std::make_shared<forge::api::core::proxy<overloaded_api>>(invoker)};

   const auto response = forge::asio::blocking::run(runtime, handle->sign({.ref = "payload"}));

   BOOST_TEST(response.bytes == "remote:payload");
   BOOST_TEST(invoker->last.api.id.value == "overloaded");
   BOOST_TEST(invoker->last.api.major == 1U);
   BOOST_TEST(invoker->last.api.min_revision == 3U);
   BOOST_TEST(invoker->last.method == "sign");
}

BOOST_AUTO_TEST_CASE(generated_proxy_invokes_positional_method) {
   auto runtime = forge::asio::runtime{};
   auto invoker = std::make_shared<recording_positional_invoker>();
   auto handle =
       forge::api::core::handle<positional_api>{std::make_shared<forge::api::core::proxy<positional_api>>(invoker)};

   const auto response = forge::asio::blocking::run(runtime, handle->concat("a", "b"));

   BOOST_TEST(response.bytes == "remote:a:b");
   BOOST_TEST(invoker->last.api.id.value == "positional");
   BOOST_TEST(invoker->last.api.major == 1U);
   BOOST_TEST(invoker->last.api.min_revision == 2U);
   BOOST_TEST(invoker->last.method == "concat");
   const auto args = forge::api::core::unpack_body<std::tuple<std::string, std::string>>(invoker->last.body);
   BOOST_TEST(std::get<0>(args) == "a");
   BOOST_TEST(std::get<1>(args) == "b");
}

BOOST_AUTO_TEST_CASE(generated_proxy_preserves_requested_api_revision) {
   auto runtime = forge::asio::runtime{};
   auto invoker = std::make_shared<recording_invoker>();
   auto mount = recording_remote_mount{invoker};

   auto handle = forge::asio::blocking::run(runtime, mount.get_remote_api<cache_api>(cache_api::ref(2)));
   const auto response = forge::asio::blocking::run(runtime, handle->read({.ref = "abc"}));

   BOOST_TEST(response.bytes == "remote:abc");
   BOOST_TEST(mount.last_requested.min_revision == 2U);
   BOOST_TEST(invoker->last.api.id.value == "cache");
   BOOST_TEST(invoker->last.api.major == 1U);
   BOOST_TEST(invoker->last.api.min_revision == 2U);
}

BOOST_AUTO_TEST_CASE(binding_export_filters_methods_above_selected_revision) {
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   const auto plan = forge::api::core::binding().serve(registry).export_api<cache_api>(cache_api::ref(2)).build();

   BOOST_REQUIRE_EQUAL(plan.exports.size(), 1U);
   const auto& descriptor = plan.exports.front();
   BOOST_TEST(descriptor.version.revision == 2U);
   BOOST_REQUIRE(forge::api::core::find_method(descriptor, "read") != nullptr);
   BOOST_REQUIRE(forge::api::core::find_method(descriptor, "read_old") != nullptr);
   BOOST_REQUIRE(forge::api::core::find_method(descriptor, "watch") != nullptr);
   BOOST_TEST(forge::api::core::find_method(descriptor, "upload") == nullptr);
   BOOST_TEST(forge::api::core::find_method(descriptor, "sync") == nullptr);
}

BOOST_AUTO_TEST_CASE(binding_export_rejects_revision_above_implementation) {
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   auto requested = cache_api::ref();
   ++requested.min_revision;

   BOOST_CHECK_THROW(
       static_cast<void>(forge::api::core::binding().serve(registry).export_api<cache_api>(requested).build()),
       forge::api::core::exceptions::incompatible_version);
}

BOOST_AUTO_TEST_CASE(api_body_decode_rejects_trailing_bytes) {
   auto body = forge::api::core::pack_body(protocol::read_chunk{.ref = "abc"});
   body.push_back(0xff);

   BOOST_CHECK_THROW(static_cast<void>(forge::api::core::unpack_body<protocol::read_chunk>(body)),
                     forge::api::core::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(method_descriptor_records_client_and_bidirectional_stream_kinds) {
   auto descriptor =
       forge::api::core::define<cache_api>({.id = {"cache.streams"}, .version = {.major = 1, .revision = 0}})
           .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
           .bidirectional_stream<&cache_api::sync, protocol::read_chunk, protocol::chunk>("sync")
           .build();

   const auto* upload = forge::api::core::find_method(descriptor, "upload");
   const auto* sync = forge::api::core::find_method(descriptor, "sync");
   BOOST_REQUIRE(upload != nullptr);
   BOOST_REQUIRE(sync != nullptr);
   BOOST_CHECK(upload->kind == forge::api::core::method_kind::client_stream);
   BOOST_CHECK(sync->kind == forge::api::core::method_kind::bidirectional_stream);
}

BOOST_AUTO_TEST_CASE(call_runtime_rejects_duplicate_unknown_and_post_terminal_frames) {
   auto calls = forge::api::core::call_runtime{forge::api::core::call_runtime_options{.max_inflight = 1}};
   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 99},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
   };

   calls.observe(request);
   BOOST_TEST(calls.active_calls() == 1U);
   BOOST_CHECK_THROW(calls.observe(request), forge::api::core::exceptions::protocol_error);

   auto stream_item = request;
   stream_item.kind = forge::api::core::frame_kind::stream_item;
   calls.observe(stream_item);
   BOOST_TEST(calls.active_calls() == 1U);

   auto stream_end = request;
   stream_end.kind = forge::api::core::frame_kind::stream_end;
   calls.observe(stream_end);
   BOOST_TEST(calls.active_calls() == 0U);
   BOOST_CHECK_THROW(calls.observe(stream_item), forge::api::core::exceptions::protocol_error);

   auto cancel_request = request;
   cancel_request.id.value = 100;
   calls.observe(cancel_request);
   auto cancel = cancel_request;
   cancel.kind = forge::api::core::frame_kind::cancel;
   calls.observe(cancel);
   BOOST_TEST(calls.active_calls() == 0U);
}

BOOST_AUTO_TEST_CASE(call_runtime_enforces_deadline_before_non_terminal_frames) {
   auto calls = forge::api::core::call_runtime{
       forge::api::core::call_runtime_options{.max_inflight = 1, .deadline = std::chrono::milliseconds{1}}};
   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 101},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
   };

   calls.observe(request);
   std::this_thread::sleep_for(std::chrono::milliseconds{3});

   auto item = request;
   item.kind = forge::api::core::frame_kind::stream_item;
   BOOST_CHECK_THROW(calls.observe(item), forge::api::core::exceptions::deadline_exceeded);
   BOOST_TEST(calls.active_calls() == 0U);
}

BOOST_AUTO_TEST_CASE(binding_plan_runs_interceptors_in_deterministic_order) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   auto trace = std::make_shared<std::string>();
   auto plan = forge::api::core::binding()
                   .serve(registry)
                   .interceptor(forge::api::core::interceptor()
                                    .id("observe")
                                    .phase(forge::api::core::interceptor_phase::observe)
                                    .order(20)
                                    .handler([trace](forge::api::core::call_context&) -> boost::asio::awaitable<void> {
                                       *trace += "observe>";
                                       co_return;
                                    })
                                    .build())
                   .interceptor(forge::api::core::interceptor()
                                    .id("authz")
                                    .phase(forge::api::core::interceptor_phase::authorize)
                                    .order(10)
                                    .handler([trace](forge::api::core::call_context&) -> boost::asio::awaitable<void> {
                                       *trace += "authz>";
                                       co_return;
                                    })
                                    .build())
                   .build();

   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 17},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "abc"}),
   };

   const auto response = forge::asio::blocking::run(runtime, plan.dispatch(request));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(*trace == "observe>authz>");
}

BOOST_AUTO_TEST_CASE(binding_plan_dispatches_positional_method) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<positional_api>(positional_api::describe(), std::make_shared<positional_impl>());

   auto plan = forge::api::core::binding().serve(registry).build();
   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 117},
       .api = {.id = {"positional"}, .major = 1, .min_revision = 2},
       .method = "concat",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(std::make_tuple(std::string{"left"}, std::string{"right"})),
   };

   const auto response = forge::asio::blocking::run(runtime, plan.dispatch(request));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "left:right");
}

BOOST_AUTO_TEST_CASE(binding_plan_interceptor_sees_request_payload) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   auto observed = std::make_shared<std::string>();
   auto plan =
       forge::api::core::binding()
           .serve(registry)
           .interceptor(
               forge::api::core::interceptor()
                   .id("payload")
                   .phase(forge::api::core::interceptor_phase::authorize)
                   .handler([observed](forge::api::core::call_context& context) -> boost::asio::awaitable<void> {
                      *observed = forge::raw::unpack<protocol::read_chunk>(context.payload).ref;
                      co_return;
                   })
                   .build())
           .build();

   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 18},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "payload-visible"}),
   };

   const auto response = forge::asio::blocking::run(runtime, plan.dispatch(request));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(*observed == "payload-visible");
}

BOOST_AUTO_TEST_CASE(binding_plan_dispatches_server_stream_as_item_and_end_frames) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .server_stream<&cache_api::watch, protocol::read_chunk, protocol::chunk>("watch")
                         .build();
   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   auto plan = forge::api::core::binding().serve(registry).build();
   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 33},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "watch",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "abc"}),
   };

   const auto responses = forge::asio::blocking::run(runtime, plan.dispatch_many(request));

   BOOST_REQUIRE_EQUAL(responses.size(), 3U);
   BOOST_CHECK(responses[0].kind == forge::api::core::frame_kind::stream_item);
   BOOST_CHECK(responses[1].kind == forge::api::core::frame_kind::stream_item);
   BOOST_CHECK(responses[2].kind == forge::api::core::frame_kind::stream_end);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[0].payload).bytes == "abc:0");
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[1].payload).bytes == "abc:1");
}

BOOST_AUTO_TEST_CASE(binding_plan_rejects_method_above_exported_revision) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .server_stream<&cache_api::watch, protocol::read_chunk, protocol::chunk>("watch")
                         .build();
   auto watch_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor),
                               std::make_shared<tracking_cache_impl>(std::make_shared<int>(0), watch_calls));

   auto plan = forge::api::core::binding().serve(registry).export_api<cache_api>(cache_api::ref(0)).build();
   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 41},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "watch",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "abc"}),
   };

   const auto responses = forge::asio::blocking::run(runtime, plan.dispatch_many(request));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::error);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(responses.front().payload);
   BOOST_TEST(payload.error == "api_not_exported");
   BOOST_TEST(*watch_calls == 0);
}

BOOST_AUTO_TEST_CASE(binding_plan_rejects_method_above_requested_revision) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .server_stream<&cache_api::watch, protocol::read_chunk, protocol::chunk>("watch")
                         .build();
   auto watch_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor),
                               std::make_shared<tracking_cache_impl>(std::make_shared<int>(0), watch_calls));

   auto plan = forge::api::core::binding().serve(registry).export_api<cache_api>(cache_api::ref(8)).build();
   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 46},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 0},
       .method = "watch",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "abc"}),
   };

   const auto responses = forge::asio::blocking::run(runtime, plan.dispatch_many(request));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::error);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(responses.front().payload);
   BOOST_TEST(payload.error == "api_not_exported");
   BOOST_TEST(*watch_calls == 0);
}

BOOST_AUTO_TEST_CASE(binding_plan_dispatches_client_stream_as_item_sequence_and_single_response) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   auto plan = forge::api::core::binding().serve(registry).build();
   const auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 34},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };
   auto first = start;
   first.kind = forge::api::core::frame_kind::stream_item;
   first.payload = pack_api_payload(protocol::read_chunk{.ref = "a"});
   auto second = start;
   second.kind = forge::api::core::frame_kind::stream_item;
   second.payload = pack_api_payload(protocol::read_chunk{.ref = "b"});
   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;

   const auto responses = forge::asio::blocking::run(runtime, plan.dispatch_stream({start, first, second, end}));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses[0].kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[0].payload).bytes == "a,b");
}

BOOST_AUTO_TEST_CASE(api_dispatcher_clears_grouped_stream_on_cancel) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   auto dispatcher = forge::api::core::frame_dispatcher{
       forge::api::core::binding().serve(registry).build(),
       forge::api::core::dispatch_options{.max_inflight = 1},
   };
   auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 37},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };

   auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(start));
   BOOST_TEST(responses.empty());
   BOOST_TEST(dispatcher.grouped_calls() == 1U);
   BOOST_TEST(dispatcher.active_calls() == 1U);

   auto cancel = start;
   cancel.kind = forge::api::core::frame_kind::cancel;
   responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(cancel));
   BOOST_TEST(responses.empty());
   BOOST_TEST(dispatcher.grouped_calls() == 0U);
   BOOST_TEST(dispatcher.active_calls() == 0U);

   auto replacement = start;
   replacement.id.value = 38;
   responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(replacement));
   BOOST_TEST(responses.empty());
   BOOST_TEST(dispatcher.grouped_calls() == 1U);
   BOOST_TEST(dispatcher.active_calls() == 1U);
}

BOOST_AUTO_TEST_CASE(api_dispatcher_strips_reserved_metadata_before_interceptors) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   auto observed_reserved = std::make_shared<std::string>();
   auto observed_public = std::make_shared<std::string>();
   auto plan =
       forge::api::core::binding()
           .serve(registry)
           .interceptor(forge::api::core::interceptor()
                            .id("metadata")
                            .phase(forge::api::core::interceptor_phase::authorize)
                            .handler([observed_reserved, observed_public](
                                         forge::api::core::call_context& context) -> boost::asio::awaitable<void> {
                               *observed_reserved = forge::api::core::metadata_value(
                                                        context.meta, forge::api::core::p2p_remote_peer_metadata_key)
                                                        .value_or("missing");
                               *observed_public =
                                   forge::api::core::metadata_value(context.meta, "x-client-trace").value_or("missing");
                               co_return;
                            })
                            .build())
           .build();
   auto dispatcher = forge::api::core::frame_dispatcher{std::move(plan)};
   auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 49},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .meta =
           {
               {.key = std::string{forge::api::core::p2p_remote_peer_metadata_key}, .value = "spoofed"},
               {.key = "x-client-trace", .value = "trace-1"},
           },
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "metadata"}),
   };

   const auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(std::move(request)));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::response);
   BOOST_TEST(*observed_reserved == "missing");
   BOOST_TEST(*observed_public == "trace-1");
}

BOOST_AUTO_TEST_CASE(api_dispatcher_injects_trusted_metadata_after_scrub) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   auto observed = std::make_shared<std::string>();
   auto plan =
       forge::api::core::binding()
           .serve(registry)
           .interceptor(
               forge::api::core::interceptor()
                   .id("trusted")
                   .phase(forge::api::core::interceptor_phase::authorize)
                   .handler([observed](forge::api::core::call_context& context) -> boost::asio::awaitable<void> {
                      *observed =
                          forge::api::core::metadata_value(context.meta, forge::api::core::p2p_remote_peer_metadata_key)
                              .value_or("missing");
                      co_return;
                   })
                   .build())
           .build();
   auto dispatcher = forge::api::core::frame_dispatcher{
       std::move(plan),
       forge::api::core::dispatch_options{
           .trusted_metadata = {{.key = std::string{forge::api::core::p2p_remote_peer_metadata_key},
                                 .value = "trusted"}},
       },
   };
   auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 50},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .meta = {{.key = std::string{forge::api::core::p2p_remote_peer_metadata_key}, .value = "spoofed"}},
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "trusted"}),
   };

   const auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(std::move(request)));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::response);
   BOOST_TEST(*observed == "trusted");
}

BOOST_AUTO_TEST_CASE(api_dispatcher_sanitizes_grouped_stream_metadata) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   auto observed = std::make_shared<std::string>();
   auto plan =
       forge::api::core::binding()
           .serve(registry)
           .interceptor(
               forge::api::core::interceptor()
                   .id("stream-metadata")
                   .phase(forge::api::core::interceptor_phase::authorize)
                   .handler([observed](forge::api::core::call_context& context) -> boost::asio::awaitable<void> {
                      *observed =
                          forge::api::core::metadata_value(context.meta, forge::api::core::p2p_remote_peer_metadata_key)
                              .value_or("missing");
                      co_return;
                   })
                   .build())
           .build();
   auto dispatcher = forge::api::core::frame_dispatcher{
       std::move(plan),
       forge::api::core::dispatch_options{
           .trusted_metadata = {{.key = std::string{forge::api::core::p2p_remote_peer_metadata_key},
                                 .value = "trusted-stream"}},
       },
   };
   auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 51},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .meta = {{.key = std::string{forge::api::core::p2p_remote_peer_metadata_key}, .value = "spoofed-stream"}},
       .codec = {.value = "forge.raw"},
   };
   auto item = start;
   item.kind = forge::api::core::frame_kind::stream_item;
   item.payload = pack_api_payload(protocol::read_chunk{.ref = "a"});
   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;

   auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(start));
   BOOST_TEST(responses.empty());
   responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(item));
   BOOST_TEST(responses.empty());
   responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(end));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::response);
   BOOST_TEST(*observed == "trusted-stream");
}

BOOST_AUTO_TEST_CASE(api_dispatcher_observes_grouped_stream_end_before_dispatch) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   auto upload_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor), std::make_shared<tracking_cache_impl>(upload_calls));

   auto dispatcher = forge::api::core::frame_dispatcher{
       forge::api::core::binding().serve(registry).build(),
       forge::api::core::dispatch_options{.max_inflight = 1, .deadline = std::chrono::milliseconds{1}},
   };
   auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 39},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };

   auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(start));
   BOOST_TEST(responses.empty());
   BOOST_TEST(dispatcher.grouped_calls() == 1U);
   BOOST_TEST(dispatcher.active_calls() == 1U);

   std::this_thread::sleep_for(std::chrono::milliseconds{3});

   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, dispatcher.dispatch(end)),
                     forge::api::core::exceptions::deadline_exceeded);
   BOOST_TEST(dispatcher.grouped_calls() == 0U);
   BOOST_TEST(dispatcher.active_calls() == 0U);
   BOOST_TEST(*upload_calls == 0);
}

BOOST_AUTO_TEST_CASE(binding_plan_releases_preobserved_grouped_call_on_export_denial) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   auto upload_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor), std::make_shared<tracking_cache_impl>(upload_calls));

   auto plan = forge::api::core::binding()
                   .serve(registry)
                   .export_api<remote_only_api>({.id = {"remote.only"}, .major = 1, .min_revision = 0})
                   .build();
   auto calls = forge::api::core::call_runtime{forge::api::core::call_runtime_options{.max_inflight = 1}};
   auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 43},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };

   calls.observe(start);
   BOOST_TEST(calls.active_calls() == 1U);

   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;
   auto responses = forge::asio::blocking::run(runtime, plan.dispatch_stream({start, end}, calls));
   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses[0].kind == forge::api::core::frame_kind::error);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(responses[0].payload);
   BOOST_TEST(payload.error == "api_not_exported");
   BOOST_TEST(calls.active_calls() == 0U);
   BOOST_TEST(*upload_calls == 0);

   auto replacement = start;
   replacement.id.value = 44;
   calls.observe(replacement);
   BOOST_TEST(calls.active_calls() == 1U);
}

BOOST_AUTO_TEST_CASE(api_dispatcher_does_not_group_future_client_stream_method) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   auto upload_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor), std::make_shared<tracking_cache_impl>(upload_calls));

   auto dispatcher = forge::api::core::frame_dispatcher{
       forge::api::core::binding().serve(registry).export_api<cache_api>(cache_api::ref(2)).build(),
       forge::api::core::dispatch_options{.max_inflight = 1},
   };
   const auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 45},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 2},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };

   const auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(start));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::error);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(responses.front().payload);
   BOOST_TEST(payload.error == "api_not_exported");
   BOOST_TEST(dispatcher.grouped_calls() == 0U);
   BOOST_TEST(dispatcher.active_calls() == 0U);
   BOOST_TEST(*upload_calls == 0);
}

BOOST_AUTO_TEST_CASE(api_dispatcher_does_not_group_method_above_requested_revision) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   auto upload_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor), std::make_shared<tracking_cache_impl>(upload_calls));

   auto dispatcher = forge::api::core::frame_dispatcher{
       forge::api::core::binding().serve(registry).export_api<cache_api>(cache_api::ref(8)).build(),
       forge::api::core::dispatch_options{.max_inflight = 1},
   };
   const auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 47},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 2},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };

   const auto responses = forge::asio::blocking::run(runtime, dispatcher.dispatch(start));

   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   BOOST_CHECK(responses.front().kind == forge::api::core::frame_kind::error);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(responses.front().payload);
   BOOST_TEST(payload.error == "api_not_exported");
   BOOST_TEST(dispatcher.grouped_calls() == 0U);
   BOOST_TEST(dispatcher.active_calls() == 0U);
   BOOST_TEST(*upload_calls == 0);
}

BOOST_AUTO_TEST_CASE(binding_plan_dispatch_stream_honors_preobserved_runtime_deadline) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   auto plan = forge::api::core::binding().serve(registry).build();
   const auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 36},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };
   auto item = start;
   item.kind = forge::api::core::frame_kind::stream_item;
   item.payload = pack_api_payload(protocol::read_chunk{.ref = "late"});
   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;

   auto calls = forge::api::core::call_runtime{
       forge::api::core::call_runtime_options{.max_inflight = 1, .deadline = std::chrono::milliseconds{1}}};
   calls.observe(start);
   std::this_thread::sleep_for(std::chrono::milliseconds{3});

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, plan.dispatch_stream({start, item, end}, calls)),
                     forge::api::core::exceptions::deadline_exceeded);
}

BOOST_AUTO_TEST_CASE(binding_plan_dispatch_stream_checks_terminal_stream_end_deadline) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .client_stream<&cache_api::upload, protocol::read_chunk, protocol::chunk>("upload")
                         .build();
   auto upload_calls = std::make_shared<int>(0);
   registry.install<cache_api>(std::move(descriptor), std::make_shared<tracking_cache_impl>(upload_calls));

   auto plan = forge::api::core::binding().serve(registry).build();
   const auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 40},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "upload",
       .codec = {.value = "forge.raw"},
   };
   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;

   auto calls = forge::api::core::call_runtime{
       forge::api::core::call_runtime_options{.max_inflight = 1, .deadline = std::chrono::milliseconds{1}}};
   calls.observe(start);
   std::this_thread::sleep_for(std::chrono::milliseconds{3});

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, plan.dispatch_stream({start, end}, calls)),
                     forge::api::core::exceptions::deadline_exceeded);
   BOOST_TEST(calls.active_calls() == 0U);
   BOOST_TEST(*upload_calls == 0);
}

BOOST_AUTO_TEST_CASE(binding_plan_dispatches_bidirectional_stream_as_item_and_end_frames) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto descriptor = forge::api::core::define<cache_api>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
                         .bidirectional_stream<&cache_api::sync, protocol::read_chunk, protocol::chunk>("sync")
                         .build();
   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   auto plan = forge::api::core::binding().serve(registry).build();
   const auto start = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 35},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "sync",
       .codec = {.value = "forge.raw"},
   };
   auto first = start;
   first.kind = forge::api::core::frame_kind::stream_item;
   first.payload = pack_api_payload(protocol::read_chunk{.ref = "a"});
   auto second = start;
   second.kind = forge::api::core::frame_kind::stream_item;
   second.payload = pack_api_payload(protocol::read_chunk{.ref = "b"});
   auto end = start;
   end.kind = forge::api::core::frame_kind::stream_end;

   const auto responses = forge::asio::blocking::run(runtime, plan.dispatch_stream({start, first, second, end}));

   BOOST_REQUIRE_EQUAL(responses.size(), 3U);
   BOOST_CHECK(responses[0].kind == forge::api::core::frame_kind::stream_item);
   BOOST_CHECK(responses[1].kind == forge::api::core::frame_kind::stream_item);
   BOOST_CHECK(responses[2].kind == forge::api::core::frame_kind::stream_end);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[0].payload).bytes == "a:ack");
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(responses[1].payload).bytes == "b:ack");
}

BOOST_AUTO_TEST_CASE(binding_plan_exports_are_enforced_when_declared) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   auto plan = forge::api::core::binding()
                   .serve(forge::api::core::view{registry})
                   .export_api<cache_api>({.id = {"cache"}, .major = 1, .min_revision = 8})
                   .build();

   const auto exported_request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 41},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "visible"}),
   };
   const auto exported_response = forge::asio::blocking::run(runtime, plan.dispatch(exported_request));
   BOOST_CHECK(exported_response.kind == forge::api::core::frame_kind::response);

   auto hidden_request = exported_request;
   hidden_request.id.value = 42;
   hidden_request.api.id.value = "hidden";

   const auto hidden_response = forge::asio::blocking::run(runtime, plan.dispatch(hidden_request));
   BOOST_CHECK(hidden_response.kind == forge::api::core::frame_kind::error);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(hidden_response.payload);
   BOOST_TEST(payload.error == "api_not_exported");
}

BOOST_AUTO_TEST_CASE(descriptor_declared_exception_maps_to_error_payload) {
   const auto descriptor = cache_descriptor_with_declared_errors();
   const auto* method = forge::api::core::find_method(descriptor, "read");
   BOOST_REQUIRE(method != nullptr);

   try {
      FORGE_THROW_EXCEPTION(cache_errors::chunk_not_found, "chunk not found", forge::exceptions::ctx("ref", "bafk..."));
   } catch (const forge::exceptions::base& error) {
      const auto payload = forge::api::core::project_error(*method, error);

      BOOST_TEST(payload.error == "chunk_not_found");
      BOOST_TEST(payload.message == "chunk not found");
      BOOST_TEST(payload.identity.category == "test.cache");
      BOOST_TEST(payload.identity.code == 1u);
      return;
   }

   BOOST_FAIL("expected typed API exception");
}

BOOST_AUTO_TEST_CASE(contract_rejects_empty_api_id) {
   BOOST_CHECK_THROW(build_empty_id_descriptor(), forge::api::core::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(contract_rejects_zero_major_version) {
   BOOST_CHECK_THROW(build_zero_major_descriptor(), forge::api::core::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(contract_rejects_duplicate_method_name) {
   BOOST_CHECK_THROW(build_duplicate_method_descriptor(), forge::api::core::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(local_registry_view_returns_typed_handle) {
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   const auto view = forge::api::core::view{registry};
   const auto handle = view.get<cache_api>({.id = {"cache"}, .major = 1, .min_revision = 8});

   BOOST_TEST(static_cast<bool>(handle));
   BOOST_TEST(registry.describe({.id = {"cache"}, .major = 1, .min_revision = 8}) != nullptr);
}

BOOST_AUTO_TEST_CASE(registry_install_normalizes_custom_descriptor_surfaces) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto generated = cache_api::describe();
   auto descriptor = forge::api::core::descriptor{
       .id = {"cache.alias"},
       .version = generated.version,
       .methods = std::move(generated.methods),
   };
   BOOST_CHECK(!forge::api::core::supports(descriptor.supported_surfaces, forge::api::core::surface::remote));

   registry.install<cache_api>(std::move(descriptor), std::make_shared<cache_impl>());

   const auto requested = forge::api::core::api_ref{
       .id = {"cache.alias"},
       .major = 1,
       .min_revision = 8,
   };
   const auto* installed = registry.describe(requested);
   BOOST_REQUIRE(installed != nullptr);
   BOOST_CHECK(forge::api::core::supports(installed->supported_surfaces, forge::api::core::surface::local));
   BOOST_CHECK(forge::api::core::supports(installed->supported_surfaces, forge::api::core::surface::remote));

   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 8},
       .api = requested,
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "alias"}),
   };
   const auto response = forge::asio::blocking::run(runtime, registry.dispatch(request));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(forge::raw::unpack<protocol::chunk>(response.payload).bytes == "alias");
}

BOOST_AUTO_TEST_CASE(version_lookup_rejects_too_old_revision) {
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   const auto view = forge::api::core::view{registry};

   BOOST_TEST(!view.try_get<cache_api>({.id = {"cache"}, .major = 1, .min_revision = 9}));
}

BOOST_AUTO_TEST_CASE(registry_dispatch_invokes_typed_method_over_raw_frame) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());

   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 7},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "abc"}),
   };

   const auto response = forge::asio::blocking::run(runtime, registry.dispatch(request));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::response);
   BOOST_TEST(response.id.value == 7u);
   const auto chunk = forge::raw::unpack<protocol::chunk>(response.payload);
   BOOST_TEST(chunk.bytes == "abc");
}

BOOST_AUTO_TEST_CASE(registry_builtin_errors_set_semantic_status) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 9},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .codec = {.value = "forge.raw"},
   };

   auto response = forge::asio::blocking::run(runtime, registry.dispatch(request));
   auto payload = forge::raw::unpack<forge::api::core::error_payload>(response.payload);
   BOOST_CHECK(response.kind == forge::api::core::frame_kind::error);
   BOOST_CHECK(payload.status_code == forge::api::core::status::failed_precondition);
   BOOST_TEST(payload.identity.code ==
              static_cast<std::uint32_t>(forge::api::core::exceptions::code::incompatible_version));

   registry.install<cache_api>(cache_api::describe(), std::make_shared<cache_impl>());
   request.method = "missing";
   response = forge::asio::blocking::run(runtime, registry.dispatch(request));
   payload = forge::raw::unpack<forge::api::core::error_payload>(response.payload);
   BOOST_CHECK(payload.status_code == forge::api::core::status::not_found);
   BOOST_TEST(payload.identity.code ==
              static_cast<std::uint32_t>(forge::api::core::exceptions::code::method_not_found));

   request.kind = forge::api::core::frame_kind::stream_item;
   auto responses = forge::asio::blocking::run(runtime, registry.dispatch_stream({request}));
   BOOST_REQUIRE_EQUAL(responses.size(), 1U);
   payload = forge::raw::unpack<forge::api::core::error_payload>(responses.front().payload);
   BOOST_CHECK(payload.status_code == forge::api::core::status::invalid_argument);
   BOOST_TEST(payload.identity.code == static_cast<std::uint32_t>(forge::api::core::exceptions::code::protocol_error));
}

class throwing_cache_impl final : public cache_api {
 public:
   boost::asio::awaitable<protocol::chunk> read(protocol::read_chunk) override {
      FORGE_THROW_EXCEPTION(cache_errors::chunk_not_found, "chunk not found", forge::exceptions::ctx("ref", "abc"));
   }

   boost::asio::awaitable<protocol::chunk> read_old(protocol::read_old_request) override {
      FORGE_THROW_EXCEPTION(cache_errors::chunk_not_found, "chunk not found", forge::exceptions::ctx("ref", "abc"));
   }

   boost::asio::awaitable<std::vector<protocol::chunk>> watch(protocol::read_chunk) override {
      FORGE_THROW_EXCEPTION(cache_errors::chunk_not_found, "chunk not found", forge::exceptions::ctx("ref", "abc"));
   }

   boost::asio::awaitable<protocol::chunk> upload(std::vector<protocol::read_chunk>) override {
      FORGE_THROW_EXCEPTION(cache_errors::chunk_not_found, "chunk not found", forge::exceptions::ctx("ref", "abc"));
   }

   boost::asio::awaitable<std::vector<protocol::chunk>> sync(std::vector<protocol::read_chunk>) override {
      FORGE_THROW_EXCEPTION(cache_errors::chunk_not_found, "chunk not found", forge::exceptions::ctx("ref", "abc"));
   }
};

BOOST_AUTO_TEST_CASE(registry_dispatch_maps_declared_exception_to_error_frame) {
   auto runtime = forge::asio::runtime{};
   auto registry = forge::api::core::registry{};
   registry.install<cache_api>(cache_descriptor_with_declared_errors(), std::make_shared<throwing_cache_impl>());

   const auto request = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::request,
       .id = {.value = 8},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(protocol::read_chunk{.ref = "abc"}),
   };

   const auto response = forge::asio::blocking::run(runtime, registry.dispatch(request));

   BOOST_CHECK(response.kind == forge::api::core::frame_kind::error);
   BOOST_TEST(response.id.value == 8u);
   const auto payload = forge::raw::unpack<forge::api::core::error_payload>(response.payload);
   BOOST_TEST(payload.error == "chunk_not_found");
   BOOST_TEST(payload.identity.category == "test.cache");
   BOOST_TEST(payload.identity.code == 1u);
}

BOOST_AUTO_TEST_CASE(remote_declared_exception_restores_typed_exception) {
   const auto descriptor = cache_descriptor_with_declared_errors();
   const auto* method = forge::api::core::find_method(descriptor, "read");
   BOOST_REQUIRE(method != nullptr);

   const auto payload = forge::api::core::error_payload{
       .error = "chunk_not_found",
       .message = "chunk not found",
       .retryable = false,
       .status_code = forge::api::core::status::not_found,
       .identity = {.category = "test.cache", .code = 1},
   };

   BOOST_CHECK_THROW(forge::api::core::raise_remote_error(payload, method), cache_errors::chunk_not_found);
}

BOOST_AUTO_TEST_CASE(remote_core_exception_restores_typed_exception) {
   const auto payload = forge::api::core::make_core_error_payload(forge::api::core::exceptions::code::deadline_exceeded,
                                                                  "remote deadline expired");

   BOOST_CHECK_THROW(forge::api::core::raise_remote_error(payload), forge::api::core::exceptions::deadline_exceeded);
}

BOOST_AUTO_TEST_CASE(remote_unknown_exception_preserves_identity_in_generic_error) {
   const auto payload = forge::api::core::error_payload{
       .error = "peer_exploded",
       .message = "remote failed",
       .retryable = false,
       .status_code = forge::api::core::status::internal,
       .identity = {.category = "remote.peer", .code = 77},
   };

   try {
      forge::api::core::raise_remote_error(payload);
   } catch (const forge::api::core::exceptions::remote_internal& error) {
      BOOST_TEST(error.code().category().name() == std::string{"forge.api"});
      BOOST_TEST(error.message() == "remote failed");
      BOOST_REQUIRE(error.context().size() >= 3);
      BOOST_TEST(error.context()[1].value == "remote.peer");
      BOOST_TEST(error.context()[2].value == "77");
      return;
   }

   BOOST_FAIL("expected generic remote API exception");
}

BOOST_AUTO_TEST_SUITE_END()
