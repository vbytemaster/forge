#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>
#include <forge/api/macros.hpp>
#include <forge/exceptions/macros.hpp>
#include <forge/http_api/macros.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.http.api.binding;
import forge.http.base_url;
import forge.http.api.parameters;
import forge.http.body;
import forge.http.client;
import forge.http.connection;
import forge.http.exceptions;
import forge.http.file;
import forge.http.api.mapping;
import forge.http.middleware;
import forge.http.negotiation;
import forge.http.api.proxy;
import forge.http.range;
import forge.http.route_context;
import forge.http.router;
import forge.http.server;
import forge.http.stream;
import forge.http.target;
import forge.http.types;
import forge.http.upload;
import forge.json;
import forge.raw.raw;
import forge.schema.object;
import forge.xml;
import forge.websocket.api;
import forge.websocket.client;
import forge.websocket.connection;
import forge.websocket.exceptions;

namespace forge::http {
namespace test_api {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace beast_websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

using raw_mount_step = std::function<void(forge::http::router&, std::string_view)>;
static_assert(!std::is_constructible_v<forge::http::api::binding_plan, std::vector<raw_mount_step>>);
static_assert(!std::is_same_v<forge::http::request, boost::beast::http::request<boost::beast::http::string_body>>);
static_assert(!std::is_same_v<forge::http::response, boost::beast::http::response<boost::beast::http::string_body>>);

BOOST_AUTO_TEST_CASE(http_negotiation_matches_media_types_suffixes_and_accept_quality) {
   constexpr auto json = std::array{media_type_match{.type = "application/json", .structured_suffix = "+json"}};
   constexpr auto xml = std::array{
      media_type_match{.type = "application/xml", .structured_suffix = "+xml"},
      media_type_match{.type = "text/xml", .structured_suffix = {}},
   };

   BOOST_TEST(media_type_matches("application/json; charset=utf-8", json));
   BOOST_TEST(media_type_matches("application/problem+json", json));
   BOOST_TEST(!media_type_matches("application/xml", json));
   BOOST_TEST(media_type_matches("text/xml; charset=utf-8", xml));
   BOOST_TEST(media_type_matches("application/custom+xml", xml));
   BOOST_TEST(accept_allows("application/json;q=0, application/xml;q=0.9", xml));
   BOOST_TEST(!accept_allows("application/json;q=1, application/xml;q=0, text/xml;q=0", xml));
   BOOST_TEST(accept_allows("text/*;q=0.5", xml));
   BOOST_TEST(accept_allows("*/*", json));
}

[[nodiscard]] method to_http_method(beast_http::verb value) noexcept {
   switch (value) {
   case beast_http::verb::delete_:
      return method::delete_;
   case beast_http::verb::get:
      return method::get;
   case beast_http::verb::head:
      return method::head;
   case beast_http::verb::options:
      return method::options;
   case beast_http::verb::patch:
      return method::patch;
   case beast_http::verb::post:
      return method::post;
   case beast_http::verb::put:
      return method::put;
   default:
      return method::unknown;
   }
}

[[nodiscard]] beast_http::verb to_beast_method(method value) noexcept {
   switch (value) {
   case method::delete_:
      return beast_http::verb::delete_;
   case method::get:
      return beast_http::verb::get;
   case method::head:
      return beast_http::verb::head;
   case method::options:
      return beast_http::verb::options;
   case method::patch:
      return beast_http::verb::patch;
   case method::post:
      return beast_http::verb::post;
   case method::put:
      return beast_http::verb::put;
   case method::unknown:
      return beast_http::verb::unknown;
   }
   return beast_http::verb::unknown;
}

[[nodiscard]] request to_http_request(const beast_http::request<beast_http::string_body>& source) {
   auto target = request{to_http_method(source.method()), std::string{source.target()}, source.version()};
   target.keep_alive(source.keep_alive());
   for (const auto& header : source) {
      target.insert(header.name_string(), header.value());
   }
   target.body() = source.body();
   return target;
}

[[nodiscard]] response to_http_response(const beast_http::response<beast_http::string_body>& source) {
   auto target = response{static_cast<status>(source.result_int()), source.version()};
   target.keep_alive(source.keep_alive());
   for (const auto& header : source) {
      target.insert(header.name_string(), header.value());
   }
   target.body() = source.body();
   return target;
}

[[nodiscard]] beast_http::response<beast_http::string_body> to_beast_response(const response& source) {
   auto target = beast_http::response<beast_http::string_body>{
      static_cast<beast_http::status>(source.result_int()), source.version()};
   target.keep_alive(source.keep_alive());
   for (const auto& header : source.headers()) {
      target.insert(header.name, header.text);
   }
   target.body() = source.body();
   return target;
}

namespace api_errors {

enum class code : std::uint8_t {
   chunk_not_found = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "test.http.cache")

using chunk_not_found = forge::exceptions::coded_exception<code, code::chunk_not_found>;

} // namespace api_errors

struct api_read_chunk {};
struct api_routed_read_chunk {
   std::string ref;
   std::uint32_t offset = 0;
   std::uint32_t limit = 0;
};
struct api_chunk {
   std::string bytes;
};

struct macro_read_request {
   std::string ref;
   std::uint32_t offset = 0;
   std::uint32_t limit = 0;
};

struct macro_write_request {
   std::string ref;
   std::string bytes;
};

struct macro_chunk {
   std::string bytes;
};

struct search_request {
   std::string term;
   std::uint32_t limit = 0;
};

struct search_response {
   std::string value;
};

struct positional_http_response {
   std::string value;
};

struct positional_body_payload {
   std::string value;
};

struct positional_ref_payload {
   std::string ref;
   std::string value;
};

struct positional_body_response {
   std::string summary;
};

struct dto_http_request {
   std::string ref;
   forge::http::query<std::uint32_t> limit;
   forge::http::header<std::string> request_id;
   forge::http::cookie<std::string> session;
   forge::http::body<positional_body_payload> payload;
};

struct dto_bytes_request {
   std::string ref;
   forge::http::body_bytes bytes;
};

struct dto_multipart_request {
   forge::http::form<std::string> category;
   forge::http::form_field<std::uint32_t> count;
   forge::http::upload_file file;
};

struct dto_ambiguous_body_request {
   std::string ref;
   forge::http::body<positional_body_payload> payload;
   forge::http::body_bytes bytes;
};

struct object_put_request {
   std::string collection;
   std::string key;
   forge::http::header<std::string> content_type;
   forge::http::header<std::string> digest;
   forge::http::body_stream body;
};

struct object_put_response {
   std::uint64_t bytes = 0;
   std::string content_type;
   std::string content_md5;
};

struct object_get_request {
   std::string collection;
   std::string key;
};

struct form_submit_request {
   forge::http::form_field<std::string> label;
   forge::http::form_field<std::uint32_t> count;
};

struct form_submit_response {
   std::string summary;
};

struct control_request {
   std::string id;
};

struct control_patch_request {
   std::string id;
   std::string value;
};

struct delete_body_request {
   std::string ref;
   forge::http::body<positional_body_payload> payload;
};

struct delete_path_request : forge::http::endpoint_request {
   std::string collection;
   std::string key;
};

struct delete_stream_request {
   std::string ref;
   forge::http::body_stream body;
};

struct control_response {
   std::string value;
};

struct default_header_request {
   forge::http::header<std::string> request_id;
   forge::http::body_stream body;
};

struct default_header_response {
   std::string request_id;
   std::string body;
   bool present = false;
};

struct json_stream_request {
   std::string id;
   std::string value;
};

struct endpoint_control_request : forge::http::endpoint_request {
   std::string id;
};

struct endpoint_control_response {
   std::string summary;
};

struct stream_buffered_request : forge::http::endpoint_request {
   std::string id;
   forge::http::body_stream body;
};

struct mixed_download_request {
   std::string collection;
   std::string key;
};

BOOST_DESCRIBE_STRUCT(api_read_chunk, (), ())
BOOST_DESCRIBE_STRUCT(api_routed_read_chunk, (), (ref, offset, limit))
BOOST_DESCRIBE_STRUCT(api_chunk, (), (bytes))
BOOST_DESCRIBE_STRUCT(macro_read_request, (), (ref, offset, limit))
BOOST_DESCRIBE_STRUCT(macro_write_request, (), (ref, bytes))
BOOST_DESCRIBE_STRUCT(macro_chunk, (), (bytes))
BOOST_DESCRIBE_STRUCT(search_request, (), (term, limit))
BOOST_DESCRIBE_STRUCT(search_response, (), (value))
BOOST_DESCRIBE_STRUCT(positional_http_response, (), (value))
BOOST_DESCRIBE_STRUCT(positional_body_payload, (), (value))
BOOST_DESCRIBE_STRUCT(positional_ref_payload, (), (ref, value))
BOOST_DESCRIBE_STRUCT(positional_body_response, (), (summary))
BOOST_DESCRIBE_STRUCT(dto_http_request, (), (ref, limit, request_id, session, payload))
BOOST_DESCRIBE_STRUCT(dto_bytes_request, (), (ref, bytes))
BOOST_DESCRIBE_STRUCT(dto_multipart_request, (), (category, count, file))
BOOST_DESCRIBE_STRUCT(dto_ambiguous_body_request, (), (ref, payload, bytes))
BOOST_DESCRIBE_STRUCT(object_put_request, (), (collection, key, content_type, digest, body))
BOOST_DESCRIBE_STRUCT(object_put_response, (), (bytes, content_type, content_md5))
BOOST_DESCRIBE_STRUCT(object_get_request, (), (collection, key))
BOOST_DESCRIBE_STRUCT(form_submit_request, (), (label, count))
BOOST_DESCRIBE_STRUCT(form_submit_response, (), (summary))
BOOST_DESCRIBE_STRUCT(control_request, (), (id))
BOOST_DESCRIBE_STRUCT(control_patch_request, (), (id, value))
BOOST_DESCRIBE_STRUCT(delete_body_request, (), (ref, payload))
BOOST_DESCRIBE_STRUCT(delete_path_request, (), (collection, key))
BOOST_DESCRIBE_STRUCT(delete_stream_request, (), (ref, body))
BOOST_DESCRIBE_STRUCT(control_response, (), (value))
BOOST_DESCRIBE_STRUCT(default_header_request, (), (request_id, body))
BOOST_DESCRIBE_STRUCT(default_header_response, (), (request_id, body, present))
BOOST_DESCRIBE_STRUCT(json_stream_request, (), (id, value))
BOOST_DESCRIBE_STRUCT(endpoint_control_request, (), (id))
BOOST_DESCRIBE_STRUCT(endpoint_control_response, (), (summary))
BOOST_DESCRIBE_STRUCT(stream_buffered_request, (), (id, body))
BOOST_DESCRIBE_STRUCT(mixed_download_request, (), (collection, key))

class api_cache : public forge::api::contract<api_cache, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~api_cache() = default;

   virtual boost::asio::awaitable<api_chunk> read(api_read_chunk request) = 0;
   virtual boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk request) = 0;
   virtual boost::asio::awaitable<api_chunk> write(api_chunk request) = 0;
};

class websocket_positional_api
    : public forge::api::contract<websocket_positional_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~websocket_positional_api() = default;

   virtual boost::asio::awaitable<api_chunk> join(std::string left, std::string right) = 0;
};

class macro_cache : public forge::api::contract<macro_cache, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~macro_cache() = default;

   virtual boost::asio::awaitable<macro_chunk> read(macro_read_request request) = 0;
   virtual boost::asio::awaitable<macro_chunk> write(macro_write_request request) = 0;
};

class xml_cache_api : public forge::api::contract<xml_cache_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~xml_cache_api() = default;

   virtual boost::asio::awaitable<macro_chunk> read(macro_read_request request) = 0;
   virtual boost::asio::awaitable<macro_chunk> write(macro_write_request request) = 0;
};

class search_api : public forge::api::contract<search_api> {
 public:
   virtual ~search_api() = default;

   virtual boost::asio::awaitable<search_response> search(search_request request) = 0;
};

class positional_http_api : public forge::api::contract<positional_http_api> {
 public:
   virtual ~positional_http_api() = default;

   virtual boost::asio::awaitable<positional_http_response>
   read(std::string ref,
        forge::http::query<std::uint32_t> limit,
        forge::http::header<std::string> request_id,
        forge::http::cookie<std::string> session) = 0;
};

class positional_body_api : public forge::api::contract<positional_body_api> {
 public:
   virtual ~positional_body_api() = default;

   virtual boost::asio::awaitable<positional_body_response>
   write(std::string ref, forge::http::body<positional_body_payload> payload) = 0;
};

class positional_single_query_api : public forge::api::contract<positional_single_query_api> {
 public:
   virtual ~positional_single_query_api() = default;

   virtual boost::asio::awaitable<positional_http_response>
   read(forge::http::query<std::uint32_t> limit) = 0;
};

class positional_query_append_api : public forge::api::contract<positional_query_append_api> {
 public:
   virtual ~positional_query_append_api() = default;

   virtual boost::asio::awaitable<positional_http_response>
   read(std::string ref, forge::http::query<std::uint32_t> limit) = 0;
};

class positional_plain_body_api : public forge::api::contract<positional_plain_body_api> {
 public:
   virtual ~positional_plain_body_api() = default;

   virtual boost::asio::awaitable<positional_body_response>
   write(std::string ref, positional_body_payload payload) = 0;
};

class positional_ambiguous_body_api : public forge::api::contract<positional_ambiguous_body_api> {
 public:
   virtual ~positional_ambiguous_body_api() = default;

   virtual boost::asio::awaitable<positional_body_response>
   write(std::string ref, positional_body_payload left, positional_body_payload right) = 0;
};

class positional_checked_body_api : public forge::api::contract<positional_checked_body_api> {
 public:
   virtual ~positional_checked_body_api() = default;

   virtual boost::asio::awaitable<positional_body_response>
   write(std::string ref, positional_ref_payload payload) = 0;
};

class positional_streaming_body_api : public forge::api::contract<positional_streaming_body_api> {
 public:
   virtual ~positional_streaming_body_api() = default;

   virtual boost::asio::awaitable<forge::http::streaming_response>
   write(std::string ref, positional_body_payload payload) = 0;
};

class positional_stream_api : public forge::api::contract<positional_stream_api> {
 public:
   virtual ~positional_stream_api() = default;

   virtual boost::asio::awaitable<positional_body_response>
   write(std::string ref, forge::http::body_stream body) = 0;
};

class dto_http_api : public forge::api::contract<dto_http_api> {
 public:
   virtual ~dto_http_api() = default;

   virtual boost::asio::awaitable<positional_body_response> write(dto_http_request request) = 0;
   virtual boost::asio::awaitable<positional_body_response> write_bytes(dto_bytes_request request) = 0;
   virtual boost::asio::awaitable<positional_body_response> upload(dto_multipart_request request) = 0;
};

class dto_ambiguous_body_api : public forge::api::contract<dto_ambiguous_body_api> {
 public:
   virtual ~dto_ambiguous_body_api() = default;

   virtual boost::asio::awaitable<positional_body_response> write(dto_ambiguous_body_request request) = 0;
};

class positional_scalar_body_api : public forge::api::contract<positional_scalar_body_api> {
 public:
   virtual ~positional_scalar_body_api() = default;

   virtual boost::asio::awaitable<positional_body_response> write(std::string ref, std::string payload) = 0;
};

class object_api : public forge::api::contract<object_api> {
 public:
   virtual ~object_api() = default;

   virtual boost::asio::awaitable<object_put_response> put_object(object_put_request request) = 0;
   virtual boost::asio::awaitable<forge::http::file_response> get_object(object_get_request request) = 0;
   virtual boost::asio::awaitable<forge::http::file_response> head_object(object_get_request request) = 0;
   virtual boost::asio::awaitable<forge::http::streaming_response> stream_object(object_get_request request) = 0;
   virtual boost::asio::awaitable<forge::http::empty_response> delete_object(object_get_request request) = 0;
};

class file_only_api : public forge::api::contract<file_only_api> {
 public:
   virtual ~file_only_api() = default;

   virtual boost::asio::awaitable<forge::http::file_response> download(object_get_request request) = 0;
};

class form_api : public forge::api::contract<form_api> {
 public:
   virtual ~form_api() = default;

   virtual boost::asio::awaitable<form_submit_response> submit(form_submit_request request) = 0;
};

class control_api : public forge::api::contract<control_api> {
 public:
   virtual ~control_api() = default;

   virtual boost::asio::awaitable<forge::http::bytes_response> bytes(control_request request) = 0;
   virtual boost::asio::awaitable<forge::http::empty_response> accepted(control_request request) = 0;
   virtual boost::asio::awaitable<forge::http::empty_response> head(control_request request) = 0;
};

class alias_api : public forge::api::contract<alias_api> {
 public:
   virtual ~alias_api() = default;

   virtual boost::asio::awaitable<control_response> current(control_request request) = 0;
   virtual boost::asio::awaitable<control_response> legacy(control_request request) = 0;
};

class patch_api : public forge::api::contract<patch_api> {
 public:
   virtual ~patch_api() = default;

   virtual boost::asio::awaitable<control_response> patch(control_patch_request request) = 0;
};

class delete_body_api : public forge::api::contract<delete_body_api> {
 public:
   virtual ~delete_body_api() = default;

   virtual boost::asio::awaitable<control_response> remove(delete_body_request request) = 0;
};

class delete_path_api : public forge::api::contract<delete_path_api> {
 public:
   virtual ~delete_path_api() = default;

   virtual boost::asio::awaitable<control_response> remove(delete_path_request request) = 0;
};

class delete_stream_api : public forge::api::contract<delete_stream_api> {
 public:
   virtual ~delete_stream_api() = default;

   virtual boost::asio::awaitable<control_response> remove(delete_stream_request request) = 0;
};

class default_header_api : public forge::api::contract<default_header_api> {
 public:
   virtual ~default_header_api() = default;

   virtual boost::asio::awaitable<default_header_response> echo(default_header_request request) = 0;
};

class json_stream_api : public forge::api::contract<json_stream_api> {
 public:
   virtual ~json_stream_api() = default;

   virtual boost::asio::awaitable<forge::http::streaming_response> stream(json_stream_request request) = 0;
};

class endpoint_api : public forge::api::contract<endpoint_api> {
 public:
   virtual ~endpoint_api() = default;

   virtual boost::asio::awaitable<endpoint_control_response> current(endpoint_control_request request) = 0;
   virtual boost::asio::awaitable<forge::http::file_response> download(endpoint_control_request request) = 0;
   virtual boost::asio::awaitable<forge::http::streaming_response> stream(endpoint_control_request request) = 0;
   virtual boost::asio::awaitable<forge::http::empty_response> accepted(endpoint_control_request request) = 0;
};

class stream_buffered_api : public forge::api::contract<stream_buffered_api> {
 public:
   virtual ~stream_buffered_api() = default;

   virtual boost::asio::awaitable<endpoint_control_response> write(stream_buffered_request request) = 0;
};

class mixed_proxy_api : public forge::api::contract<mixed_proxy_api> {
 public:
   virtual ~mixed_proxy_api() = default;

   virtual boost::asio::awaitable<control_response> read(std::string collection, std::string key) = 0;
   virtual boost::asio::awaitable<forge::http::file_response> download(mixed_download_request request) = 0;
};

} // namespace test_api
} // namespace forge::http

FORGE_API(::forge::http::test_api::macro_cache, FORGE_API_CONTRACT("cache.macro", 1, 0), FORGE_API_METHOD(read),
        FORGE_API_METHOD(write))

FORGE_API(::forge::http::test_api::xml_cache_api, FORGE_API_CONTRACT("cache.xml", 1, 0), FORGE_API_METHOD(read),
        FORGE_API_METHOD(write))

FORGE_API(::forge::http::test_api::websocket_positional_api, FORGE_API_CONTRACT("websocket.positional", 1, 0),
        FORGE_API_METHOD(join, left, right))

FORGE_API(::forge::http::test_api::search_api, FORGE_API_CONTRACT("search", 1, 0),
        FORGE_API_METHOD_TYPED(search, ::forge::http::test_api::search_request, ::forge::http::test_api::search_response))

FORGE_API(::forge::http::test_api::positional_http_api, FORGE_API_CONTRACT("http.positional", 1, 0),
        FORGE_API_METHOD(read, ref, limit, request_id, session))

FORGE_API(::forge::http::test_api::positional_body_api, FORGE_API_CONTRACT("http.positional.body", 1, 0),
        FORGE_API_METHOD(write, ref, payload))

FORGE_API(::forge::http::test_api::positional_single_query_api, FORGE_API_CONTRACT("http.positional.single-query", 1, 0),
        FORGE_API_METHOD(read, limit))

FORGE_API(::forge::http::test_api::positional_query_append_api, FORGE_API_CONTRACT("http.positional.query-append", 1, 0),
        FORGE_API_METHOD(read, ref, limit))

FORGE_API(::forge::http::test_api::positional_plain_body_api, FORGE_API_CONTRACT("http.positional.plain-body", 1, 0),
        FORGE_API_METHOD(write, ref, payload))

FORGE_API(::forge::http::test_api::positional_ambiguous_body_api,
        FORGE_API_CONTRACT("http.positional.ambiguous-body", 1, 0),
        FORGE_API_METHOD(write, ref, left, right))

FORGE_API(::forge::http::test_api::positional_checked_body_api, FORGE_API_CONTRACT("http.positional.checked-body", 1, 0),
        FORGE_API_METHOD(write, ref, payload))

FORGE_API(::forge::http::test_api::positional_streaming_body_api,
        FORGE_API_CONTRACT("http.positional.streaming-body", 1, 0),
        FORGE_API_METHOD(write, ref, payload))

FORGE_API(::forge::http::test_api::positional_stream_api, FORGE_API_CONTRACT("http.positional.stream", 1, 0),
        FORGE_API_METHOD(write, ref, body))

FORGE_API(::forge::http::test_api::dto_http_api, FORGE_API_CONTRACT("http.dto", 1, 0),
        FORGE_API_METHOD_TYPED(write, ::forge::http::test_api::dto_http_request,
                             ::forge::http::test_api::positional_body_response),
        FORGE_API_METHOD_TYPED(write_bytes, ::forge::http::test_api::dto_bytes_request,
                             ::forge::http::test_api::positional_body_response),
        FORGE_API_METHOD_TYPED(upload, ::forge::http::test_api::dto_multipart_request,
                             ::forge::http::test_api::positional_body_response))

FORGE_API(::forge::http::test_api::dto_ambiguous_body_api,
        FORGE_API_CONTRACT("http.dto.ambiguous-body", 1, 0),
        FORGE_API_METHOD_TYPED(write, ::forge::http::test_api::dto_ambiguous_body_request,
                             ::forge::http::test_api::positional_body_response))

FORGE_API(::forge::http::test_api::positional_scalar_body_api,
        FORGE_API_CONTRACT("http.positional.scalar-body", 1, 0),
        FORGE_API_METHOD(write, ref, payload))

FORGE_API(::forge::http::test_api::object_api, FORGE_API_CONTRACT("object", 1, 0),
        FORGE_API_METHOD_TYPED(put_object, ::forge::http::test_api::object_put_request,
                             ::forge::http::test_api::object_put_response),
        FORGE_API_METHOD_TYPED(get_object, ::forge::http::test_api::object_get_request, ::forge::http::file_response),
        FORGE_API_METHOD_TYPED(head_object, ::forge::http::test_api::object_get_request, ::forge::http::file_response),
        FORGE_API_METHOD_TYPED(stream_object, ::forge::http::test_api::object_get_request,
                             ::forge::http::streaming_response),
        FORGE_API_METHOD_TYPED(delete_object, ::forge::http::test_api::object_get_request, ::forge::http::empty_response))

FORGE_API(::forge::http::test_api::file_only_api, FORGE_API_CONTRACT("file-only", 1, 0),
        FORGE_API_METHOD_TYPED(download, ::forge::http::test_api::object_get_request, ::forge::http::file_response))

FORGE_API(::forge::http::test_api::form_api, FORGE_API_CONTRACT("form", 1, 0),
        FORGE_API_METHOD_TYPED(submit, ::forge::http::test_api::form_submit_request,
                             ::forge::http::test_api::form_submit_response))

FORGE_API(::forge::http::test_api::control_api, FORGE_API_CONTRACT("control", 1, 0),
        FORGE_API_METHOD_TYPED(bytes, ::forge::http::test_api::control_request, ::forge::http::bytes_response),
        FORGE_API_METHOD_TYPED(accepted, ::forge::http::test_api::control_request, ::forge::http::empty_response),
        FORGE_API_METHOD_TYPED(head, ::forge::http::test_api::control_request, ::forge::http::empty_response))

FORGE_API(::forge::http::test_api::alias_api, FORGE_API_CONTRACT("alias", 1, 0),
        FORGE_API_METHOD_TYPED(current, ::forge::http::test_api::control_request,
                             ::forge::http::test_api::control_response),
        FORGE_API_METHOD_TYPED(legacy, ::forge::http::test_api::control_request,
                             ::forge::http::test_api::control_response))

FORGE_API(::forge::http::test_api::patch_api, FORGE_API_CONTRACT("patch", 1, 0),
        FORGE_API_METHOD_TYPED(patch, ::forge::http::test_api::control_patch_request,
                             ::forge::http::test_api::control_response))

FORGE_API(::forge::http::test_api::delete_body_api, FORGE_API_CONTRACT("delete-body", 1, 0),
        FORGE_API_METHOD_TYPED(remove, ::forge::http::test_api::delete_body_request,
                             ::forge::http::test_api::control_response))

FORGE_API(::forge::http::test_api::delete_path_api, FORGE_API_CONTRACT("delete-path", 1, 0),
        FORGE_API_METHOD_TYPED(remove, ::forge::http::test_api::delete_path_request,
                             ::forge::http::test_api::control_response))

FORGE_API(::forge::http::test_api::delete_stream_api, FORGE_API_CONTRACT("delete-stream", 1, 0),
        FORGE_API_METHOD_TYPED(remove, ::forge::http::test_api::delete_stream_request,
                             ::forge::http::test_api::control_response))

FORGE_API(::forge::http::test_api::default_header_api, FORGE_API_CONTRACT("default-header", 1, 0),
        FORGE_API_METHOD_TYPED(echo, ::forge::http::test_api::default_header_request,
                             ::forge::http::test_api::default_header_response))

FORGE_API(::forge::http::test_api::json_stream_api, FORGE_API_CONTRACT("json-stream", 1, 0),
        FORGE_API_METHOD_TYPED(stream, ::forge::http::test_api::json_stream_request,
                             ::forge::http::streaming_response))

FORGE_API(::forge::http::test_api::endpoint_api, FORGE_API_CONTRACT("endpoint", 1, 0),
        FORGE_API_METHOD_TYPED(current, ::forge::http::test_api::endpoint_control_request,
                             ::forge::http::test_api::endpoint_control_response),
        FORGE_API_METHOD_TYPED(download, ::forge::http::test_api::endpoint_control_request,
                             ::forge::http::file_response),
        FORGE_API_METHOD_TYPED(stream, ::forge::http::test_api::endpoint_control_request,
                             ::forge::http::streaming_response),
        FORGE_API_METHOD_TYPED(accepted, ::forge::http::test_api::endpoint_control_request,
                             ::forge::http::empty_response))

FORGE_API(::forge::http::test_api::stream_buffered_api, FORGE_API_CONTRACT("stream-buffered", 1, 0),
        FORGE_API_METHOD_TYPED(write, ::forge::http::test_api::stream_buffered_request,
                             ::forge::http::test_api::endpoint_control_response))

FORGE_API(::forge::http::test_api::mixed_proxy_api, FORGE_API_CONTRACT("mixed-proxy", 1, 0),
        FORGE_API_METHOD(read, collection, key),
        FORGE_API_METHOD_TYPED(download, ::forge::http::test_api::mixed_download_request,
                             ::forge::http::file_response))

template <> struct forge::schema::rules<::forge::http::test_api::search_request> {
   [[nodiscard]] static forge::schema::object_schema<::forge::http::test_api::search_request> define() {
      auto schema = forge::schema::object<::forge::http::test_api::search_request>();
      schema.field<&::forge::http::test_api::search_request::term>("term").required().non_empty();
      schema.field<&::forge::http::test_api::search_request::limit>("limit").required().range(1, 100);
      return schema;
   }
};

template <> struct forge::schema::rules<::forge::http::test_api::positional_body_payload> {
   [[nodiscard]] static forge::schema::object_schema<::forge::http::test_api::positional_body_payload> define() {
      auto schema = forge::schema::object<::forge::http::test_api::positional_body_payload>();
      schema.field<&::forge::http::test_api::positional_body_payload::value>("value").required().non_empty();
      return schema;
   }
};

template <> struct forge::schema::rules<::forge::http::test_api::positional_ref_payload> {
   [[nodiscard]] static forge::schema::object_schema<::forge::http::test_api::positional_ref_payload> define() {
      auto schema = forge::schema::object<::forge::http::test_api::positional_ref_payload>();
      schema.field<&::forge::http::test_api::positional_ref_payload::ref>("ref").required().non_empty();
      schema.field<&::forge::http::test_api::positional_ref_payload::value>("value").required().non_empty();
      return schema;
   }
};

template <> struct forge::schema::rules<::forge::http::test_api::json_stream_request> {
   [[nodiscard]] static forge::schema::object_schema<::forge::http::test_api::json_stream_request> define() {
      auto schema = forge::schema::object<::forge::http::test_api::json_stream_request>();
      schema.field<&::forge::http::test_api::json_stream_request::id>("id").required().non_empty();
      schema.field<&::forge::http::test_api::json_stream_request::value>("value").required().non_empty();
      return schema;
   }
};

FORGE_HTTP_API(::forge::http::test_api::macro_cache,
             FORGE_HTTP_GET(read, "/cache/chunks/:ref?offset={offset}&limit={limit}"),
             FORGE_HTTP_PUT(write, "/cache/chunks/:ref", created))

FORGE_HTTP_API(::forge::http::test_api::xml_cache_api,
             FORGE_HTTP_GET(read, "/xml/cache/chunks/:ref?offset={offset}&limit={limit}",
                            FORGE_HTTP_RESPONSE_BODY(xml),
                            FORGE_HTTP_ERROR_BODY(xml)),
             FORGE_HTTP_PUT(write, "/xml/cache/chunks/:ref", created,
                            FORGE_HTTP_REQUEST_BODY(xml),
                            FORGE_HTTP_RESPONSE_BODY(xml),
                            FORGE_HTTP_ERROR_BODY(xml)))

FORGE_HTTP_API(::forge::http::test_api::search_api,
             FORGE_HTTP_GET(search, "/search/{term}?page_size={limit}"))

FORGE_HTTP_API(::forge::http::test_api::positional_http_api,
             FORGE_HTTP_GET(read, "/objects/:ref?limit={limit}"))

FORGE_HTTP_API(::forge::http::test_api::positional_body_api,
             FORGE_HTTP_POST(write, "/objects/:ref", ok))

FORGE_HTTP_API(::forge::http::test_api::positional_single_query_api,
             FORGE_HTTP_GET(read, "/single?limit={limit}"))

FORGE_HTTP_API(::forge::http::test_api::positional_query_append_api,
             FORGE_HTTP_GET(read, "/query/:ref"))

FORGE_HTTP_API(::forge::http::test_api::positional_plain_body_api,
             FORGE_HTTP_POST(write, "/plain/:ref", created))

FORGE_HTTP_API(::forge::http::test_api::positional_ambiguous_body_api,
             FORGE_HTTP_POST(write, "/ambiguous/:ref", created))

FORGE_HTTP_API(::forge::http::test_api::positional_checked_body_api,
             FORGE_HTTP_POST(write, "/checked/:ref", created))

FORGE_HTTP_API(::forge::http::test_api::positional_streaming_body_api,
             FORGE_HTTP_POST(write, "/stream-plain/:ref", ok, FORGE_HTTP_RESPONSE_STREAM))

FORGE_HTTP_API(::forge::http::test_api::positional_stream_api,
             FORGE_HTTP_PUT(write, "/streams/:ref", ok))

FORGE_HTTP_API(::forge::http::test_api::dto_http_api,
             FORGE_HTTP_POST(write, "/dto/:ref?limit={limit}", created,
                           FORGE_HTTP_HEADER(request_id, "X-Request-Id")),
             FORGE_HTTP_PUT(write_bytes, "/dto-bytes/:ref", ok),
             FORGE_HTTP_POST(upload, "/dto-upload", ok,
                           FORGE_HTTP_FORM(category, "category"),
                           FORGE_HTTP_FORM(count, "count"),
                           FORGE_HTTP_FORM(file, "file")))

FORGE_HTTP_API(::forge::http::test_api::dto_ambiguous_body_api,
             FORGE_HTTP_POST(write, "/dto-ambiguous/:ref", created))

FORGE_HTTP_API(::forge::http::test_api::positional_scalar_body_api,
             FORGE_HTTP_POST(write, "/scalar-body/:ref", created))

FORGE_HTTP_API(::forge::http::test_api::object_api,
             FORGE_HTTP_PUT(put_object, "/objects/:collection/:key", created,
                           FORGE_HTTP_BODY_STREAM(body),
                           FORGE_HTTP_HEADER(content_type, "Content-Type"),
                           FORGE_HTTP_HEADER(digest, "Content-MD5")),
             FORGE_HTTP_GET(get_object, "/objects/:collection/:key", FORGE_HTTP_RESPONSE_FILE),
             FORGE_HTTP_HEAD(head_object, "/objects/:collection/:key", FORGE_HTTP_RESPONSE_FILE),
             FORGE_HTTP_GET(stream_object, "/objects/:collection/:key/stream", FORGE_HTTP_RESPONSE_STREAM),
             FORGE_HTTP_DELETE(delete_object, "/objects/:collection/:key", no_content))

FORGE_HTTP_API(::forge::http::test_api::form_api,
             FORGE_HTTP_POST(submit, "/forms", ok,
                           FORGE_HTTP_FORM(label, "label"),
                           FORGE_HTTP_FORM(count, "count")))

FORGE_HTTP_API(::forge::http::test_api::control_api,
             FORGE_HTTP_GET(bytes, "/controls/:id/bytes"),
             FORGE_HTTP_GET(accepted, "/controls/:id/accepted"),
             FORGE_HTTP_HEAD(head, "/controls/:id"))

FORGE_HTTP_API(::forge::http::test_api::alias_api,
             FORGE_HTTP_GET(current, "/aliases/:id/current"),
             FORGE_HTTP_GET(legacy, "/aliases/:id"))

FORGE_HTTP_API(::forge::http::test_api::patch_api,
             FORGE_HTTP_PATCH(patch, "/controls/:id", ok))

FORGE_HTTP_API(::forge::http::test_api::delete_body_api,
             FORGE_HTTP_DELETE(remove, "/delete/:ref", ok))

FORGE_HTTP_API(::forge::http::test_api::delete_path_api,
             FORGE_HTTP_DELETE(remove, "/delete-path/:collection/:key", ok))

FORGE_HTTP_API(::forge::http::test_api::delete_stream_api,
             FORGE_HTTP_DELETE(remove, "/delete-stream/:ref", ok))

FORGE_HTTP_API(::forge::http::test_api::default_header_api,
             FORGE_HTTP_PUT(echo, "/headers/default", ok, FORGE_HTTP_BODY_STREAM(body)))

FORGE_HTTP_API(::forge::http::test_api::json_stream_api,
             FORGE_HTTP_POST(stream, "/json-stream/:id", ok, FORGE_HTTP_RESPONSE_STREAM))

FORGE_HTTP_API(::forge::http::test_api::endpoint_api,
             FORGE_HTTP_GET(current, "/endpoint/:id"),
             FORGE_HTTP_GET(download, "/endpoint/:id/file", FORGE_HTTP_RESPONSE_FILE),
             FORGE_HTTP_GET(stream, "/endpoint/:id/stream", FORGE_HTTP_RESPONSE_STREAM),
             FORGE_HTTP_GET(accepted, "/endpoint/:id/accepted"))

FORGE_HTTP_API(::forge::http::test_api::stream_buffered_api,
             FORGE_HTTP_PUT(write, "/stream-buffered/:id", ok, FORGE_HTTP_BODY_STREAM(body)))

FORGE_HTTP_API(::forge::http::test_api::mixed_proxy_api,
             FORGE_HTTP_GET(read, "/mixed/:collection/:key"),
             FORGE_HTTP_GET(download, "/mixed/:collection/:key/file", FORGE_HTTP_RESPONSE_FILE))

namespace forge::api {

template <> struct api_traits<::forge::http::test_api::api_cache> {
   static api_id id() {
      return api_id{.value = "cache"};
   }

   static api_version version() {
      return api_version{.major = 1, .revision = 8};
   }

   static api_ref ref(std::uint16_t min_revision = version().revision) {
      const auto value = version();
      return api_ref{.id = id(), .major = value.major, .min_revision = min_revision};
   }

   static descriptor describe() {
      using api_cache = ::forge::http::test_api::api_cache;
      return define<api_cache>(descriptor{.id = id(), .version = version(), .interface_type = typeid(api_cache)})
          .method<&api_cache::read, ::forge::http::test_api::api_read_chunk, ::forge::http::test_api::api_chunk>("read")
          .error<::forge::http::test_api::api_errors::chunk_not_found>(
             "chunk_not_found", {.status_code = status::not_found, .retryable = false})
          .method<&api_cache::routed_read, ::forge::http::test_api::api_routed_read_chunk,
                  ::forge::http::test_api::api_chunk>("routed_read")
          .method<&api_cache::write, ::forge::http::test_api::api_chunk, ::forge::http::test_api::api_chunk>("write")
          .build();
   }
};

} // namespace forge::api

namespace forge::http {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace beast_websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

namespace api_errors = test_api::api_errors;
using test_api::api_cache;
using test_api::api_chunk;
using test_api::api_read_chunk;
using test_api::websocket_positional_api;
using test_api::api_routed_read_chunk;
using test_api::macro_cache;
using test_api::macro_chunk;
using test_api::macro_read_request;
using test_api::macro_write_request;
using test_api::xml_cache_api;
using test_api::search_api;
using test_api::search_request;
using test_api::search_response;
using test_api::control_api;
using test_api::alias_api;
using test_api::control_patch_request;
using test_api::control_request;
using test_api::control_response;
using test_api::delete_body_api;
using test_api::delete_body_request;
using test_api::delete_path_api;
using test_api::delete_path_request;
using test_api::delete_stream_api;
using test_api::delete_stream_request;
using test_api::default_header_api;
using test_api::default_header_request;
using test_api::default_header_response;
using test_api::form_api;
using test_api::form_submit_request;
using test_api::form_submit_response;
using test_api::json_stream_api;
using test_api::json_stream_request;
using test_api::endpoint_api;
using test_api::endpoint_control_request;
using test_api::endpoint_control_response;
using test_api::stream_buffered_api;
using test_api::stream_buffered_request;
using test_api::mixed_download_request;
using test_api::mixed_proxy_api;
using test_api::to_beast_response;
using test_api::to_http_request;
using test_api::to_http_response;
using test_api::dto_bytes_request;
using test_api::dto_ambiguous_body_api;
using test_api::dto_ambiguous_body_request;
using test_api::dto_http_api;
using test_api::dto_http_request;
using test_api::dto_multipart_request;
using test_api::object_api;
using test_api::object_get_request;
using test_api::object_put_request;
using test_api::object_put_response;
using test_api::patch_api;
using test_api::positional_http_api;
using test_api::positional_http_response;
using test_api::positional_body_api;
using test_api::positional_body_payload;
using test_api::positional_body_response;
using test_api::positional_ambiguous_body_api;
using test_api::positional_checked_body_api;
using test_api::positional_plain_body_api;
using test_api::positional_query_append_api;
using test_api::positional_ref_payload;
using test_api::positional_single_query_api;
using test_api::positional_scalar_body_api;
using test_api::positional_streaming_body_api;
using test_api::positional_stream_api;

[[nodiscard]] forge::api::descriptor api_cache_descriptor() {
   return api_cache::describe();
}

[[nodiscard]] std::string pack_websocket_api_frame(const forge::api::frame& frame) {
   auto out = forge::api::bytes{};
   forge::raw::pack(out, frame);
   return {out.begin(), out.end()};
}

[[nodiscard]] forge::api::frame unpack_websocket_api_frame(const std::string& value) {
   const auto bytes = forge::api::bytes{value.begin(), value.end()};
   return forge::raw::unpack<forge::api::frame>(bytes);
}

[[nodiscard]] bool has_internal_forge_header(const response& value) {
   for (const auto& header : value.headers()) {
      if (header.name.starts_with("X-FORGE-")) {
         return true;
      }
   }
   return false;
}

template <typename T>
concept has_public_can_handle = requires(T& router_value, route_context& context) {
   router_value.can_handle(context);
};

template <typename T>
concept has_public_header_preflight_classifier = requires(T& router_value, route_context& context) {
   router_value.classify_header_only_rejection(context);
};

[[nodiscard]] const forge::xml::element* find_xml_child(const forge::xml::element& parent,
                                                        std::string_view name) noexcept {
   const auto found = std::find_if(parent.children.begin(), parent.children.end(), [&](const forge::xml::element& child) {
      return child.name == name;
   });
   return found == parent.children.end() ? nullptr : &*found;
}

[[nodiscard]] std::string xml_child_text(const forge::xml::element& parent, std::string_view name) {
   if (const auto* child = find_xml_child(parent, name); child != nullptr) {
      return child->text;
   }
   return {};
}

template <typename T> [[nodiscard]] forge::api::bytes pack_api_payload(const T& value) {
   return forge::api::pack_body(value);
}

class throwing_api_cache final : public api_cache {
 public:
   boost::asio::awaitable<api_chunk> read(api_read_chunk) override {
      FORGE_THROW_EXCEPTION(api_errors::chunk_not_found, "chunk not found");
   }

   boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk) override {
      FORGE_THROW_EXCEPTION(api_errors::chunk_not_found, "chunk not found");
   }

   boost::asio::awaitable<api_chunk> write(api_chunk request) override {
      co_return request;
   }
};

class routed_api_cache final : public api_cache {
 public:
   boost::asio::awaitable<api_chunk> read(api_read_chunk) override {
      co_return api_chunk{};
   }

   boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk request) override {
      co_return api_chunk{.bytes = request.ref + ":" + std::to_string(request.offset) + ":" +
                                   std::to_string(request.limit)};
   }

   boost::asio::awaitable<api_chunk> write(api_chunk request) override {
      co_return request;
   }
};

class websocket_positional_impl final : public websocket_positional_api {
 public:
   boost::asio::awaitable<api_chunk> join(std::string left, std::string right) override {
      co_return api_chunk{.bytes = std::move(left) + ":" + std::move(right) + ":ws"};
   }
};

class escaping_api_cache final : public api_cache {
 public:
   boost::asio::awaitable<api_chunk> read(api_read_chunk) override {
      FORGE_THROW_EXCEPTION(api_errors::chunk_not_found, std::string{"chunk \"missing\"\n"} + '\b' + '\0' + "not found");
   }

   boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk) override {
      FORGE_THROW_EXCEPTION(api_errors::chunk_not_found, std::string{"chunk \"missing\"\n"} + '\b' + '\0' + "not found");
   }

   boost::asio::awaitable<api_chunk> write(api_chunk request) override {
      co_return request;
   }
};

class macro_cache_impl final : public macro_cache {
 public:
   boost::asio::awaitable<macro_chunk> read(macro_read_request request) override {
      co_return macro_chunk{.bytes = request.ref + ":" + std::to_string(request.offset) + ":" +
                                     std::to_string(request.limit)};
   }

   boost::asio::awaitable<macro_chunk> write(macro_write_request request) override {
      co_return macro_chunk{.bytes = request.ref + ":" + request.bytes};
   }
};

class xml_cache_api_impl final : public xml_cache_api {
 public:
   explicit xml_cache_api_impl(std::shared_ptr<std::atomic<std::uint32_t>> writes = {})
       : writes_{std::move(writes)} {}

   boost::asio::awaitable<macro_chunk> read(macro_read_request request) override {
      co_return macro_chunk{.bytes = request.ref + ":" + std::to_string(request.offset) + ":" +
                                     std::to_string(request.limit)};
   }

   boost::asio::awaitable<macro_chunk> write(macro_write_request request) override {
      if (writes_) {
         writes_->fetch_add(1, std::memory_order_relaxed);
      }
      co_return macro_chunk{.bytes = request.ref + ":" + request.bytes};
   }

 private:
   std::shared_ptr<std::atomic<std::uint32_t>> writes_;
};

class search_api_impl final : public search_api {
 public:
   boost::asio::awaitable<search_response> search(search_request request) override {
      co_return search_response{.value = request.term + ":" + std::to_string(request.limit)};
   }
};

class positional_http_api_impl final : public positional_http_api {
 public:
   boost::asio::awaitable<positional_http_response>
   read(std::string ref,
        forge::http::query<std::uint32_t> limit,
        forge::http::header<std::string> request_id,
        forge::http::cookie<std::string> session) override {
      const auto limit_text = limit.present ? std::to_string(limit.value) : std::string{"missing-limit"};
      const auto request_text = request_id.present ? request_id.value : std::string{"missing-request"};
      const auto session_text = session.present ? session.value : std::string{"missing-session"};
      co_return positional_http_response{.value = std::move(ref) + ":" + limit_text + ":" + request_text + ":" +
                                                  session_text};
   }
};

class positional_body_api_impl final : public positional_body_api {
 public:
   boost::asio::awaitable<positional_body_response>
   write(std::string ref, forge::http::body<positional_body_payload> payload) override {
      const auto body = payload.present ? payload.value.value : std::string{"missing-body"};
      co_return positional_body_response{.summary = std::move(ref) + ":" + body};
   }
};

class positional_single_query_api_impl final : public positional_single_query_api {
 public:
   boost::asio::awaitable<positional_http_response>
   read(forge::http::query<std::uint32_t> limit) override {
      const auto limit_text = limit.present ? std::to_string(limit.value) : std::string{"missing-limit"};
      co_return positional_http_response{.value = "single:" + limit_text};
   }
};

class positional_query_append_api_impl final : public positional_query_append_api {
 public:
   boost::asio::awaitable<positional_http_response>
   read(std::string ref, forge::http::query<std::uint32_t> limit) override {
      const auto limit_text = limit.present ? std::to_string(limit.value) : std::string{"missing-limit"};
      co_return positional_http_response{.value = std::move(ref) + ":" + limit_text};
   }
};

class positional_plain_body_api_impl final : public positional_plain_body_api {
 public:
   boost::asio::awaitable<positional_body_response>
   write(std::string ref, positional_body_payload payload) override {
      co_return positional_body_response{.summary = std::move(ref) + ":" + payload.value};
   }
};

class positional_ambiguous_body_api_impl final : public positional_ambiguous_body_api {
 public:
   boost::asio::awaitable<positional_body_response>
   write(std::string ref, positional_body_payload left, positional_body_payload right) override {
      co_return positional_body_response{.summary = std::move(ref) + ":" + left.value + ":" + right.value};
   }
};

class positional_checked_body_api_impl final : public positional_checked_body_api {
 public:
   boost::asio::awaitable<positional_body_response>
   write(std::string ref, positional_ref_payload payload) override {
      co_return positional_body_response{.summary = std::move(ref) + ":" + payload.ref + ":" + payload.value};
   }
};

class positional_streaming_body_api_impl final : public positional_streaming_body_api {
 public:
   boost::asio::awaitable<forge::http::streaming_response>
   write(std::string ref, positional_body_payload payload) override {
      auto text = std::make_shared<std::string>(std::move(ref) + ":" + payload.value);
      co_return forge::http::streaming_response::from_source(
         forge::http::streaming_response_options{
            .content_type = "text/plain",
            .body =
               [text, sent = false]() mutable -> boost::asio::awaitable<std::optional<forge::http::body_chunk>> {
                  if (sent) {
                     co_return std::nullopt;
                  }
                  sent = true;
                  auto bytes = std::vector<std::byte>(text->size());
                  std::memcpy(bytes.data(), text->data(), text->size());
                  co_return forge::http::body_chunk{.bytes = std::move(bytes)};
               },
         });
   }
};

class positional_stream_api_impl final : public positional_stream_api {
 public:
   boost::asio::awaitable<positional_body_response>
   write(std::string ref, forge::http::body_stream body) override {
      auto text = co_await body.async_read_all();
      co_return positional_body_response{.summary = std::move(ref) + ":" + std::move(text)};
   }
};

class dto_http_api_impl final : public dto_http_api {
 public:
   boost::asio::awaitable<positional_body_response> write(dto_http_request request) override {
      const auto limit = request.limit.present ? std::to_string(request.limit.value) : std::string{"missing-limit"};
      const auto request_id = request.request_id.present ? request.request_id.value : std::string{"missing-request"};
      const auto session = request.session.present ? request.session.value : std::string{"missing-session"};
      const auto body = request.payload.present ? request.payload.value.value : std::string{"missing-body"};
      co_return positional_body_response{
         .summary = request.ref + ":" + limit + ":" + request_id + ":" + session + ":" + body,
      };
   }

   boost::asio::awaitable<positional_body_response> write_bytes(dto_bytes_request request) override {
      auto body = std::string{};
      body.resize(request.bytes.bytes.size());
      if (!request.bytes.bytes.empty()) {
         std::memcpy(body.data(), request.bytes.bytes.data(), request.bytes.bytes.size());
      }
      co_return positional_body_response{.summary = request.ref + ":" + body};
   }

   boost::asio::awaitable<positional_body_response> upload(dto_multipart_request request) override {
      const auto category = request.category.present ? request.category.value : std::string{"missing-category"};
      const auto count = request.count.present ? std::to_string(request.count.value) : std::string{"missing-count"};
      const auto filename = request.file.present() && request.file.part().filename.has_value()
                               ? *request.file.part().filename
                               : std::string{"missing-file"};
      const auto text = request.file.present() ? request.file.part().text() : std::string{"missing-body"};
      co_return positional_body_response{
         .summary = category + ":" + count + ":" + filename + ":" + text,
      };
   }
};

class dto_ambiguous_body_api_impl final : public dto_ambiguous_body_api {
 public:
   boost::asio::awaitable<positional_body_response> write(dto_ambiguous_body_request request) override {
      static_cast<void>(request);
      co_return positional_body_response{.summary = "unexpected"};
   }
};

class positional_scalar_body_api_impl final : public positional_scalar_body_api {
 public:
   boost::asio::awaitable<positional_body_response> write(std::string ref, std::string payload) override {
      co_return positional_body_response{.summary = std::move(ref) + ":" + std::move(payload)};
   }
};

class default_header_api_impl final : public default_header_api {
 public:
   boost::asio::awaitable<default_header_response> echo(default_header_request request) override {
      auto body = co_await request.body.async_read_all();
      co_return default_header_response{
         .request_id = request.request_id.value,
         .body = std::move(body),
         .present = request.request_id.present,
      };
   }
};

class object_api_impl final : public object_api {
 public:
   explicit object_api_impl(std::filesystem::path root) : root_{std::move(root)} {}

   boost::asio::awaitable<object_put_response> put_object(object_put_request request) override {
      auto body = co_await request.body.async_read_all();
      auto path = object_path(request.collection, request.key);
      std::filesystem::create_directories(path.parent_path());
      auto output = std::ofstream{path, std::ios::binary};
      output << body;
      co_return object_put_response{
         .bytes = static_cast<std::uint64_t>(body.size()),
         .content_type = request.content_type.present ? request.content_type.value : std::string{},
         .content_md5 = request.digest.present ? request.digest.value : std::string{},
      };
   }

   boost::asio::awaitable<forge::http::file_response> get_object(object_get_request request) override {
      co_return forge::http::file_response::from_path(
         object_path(request.collection, request.key),
         forge::http::file_options{.content_type = "application/octet-stream"});
   }

   boost::asio::awaitable<forge::http::file_response> head_object(object_get_request request) override {
      co_return co_await get_object(std::move(request));
   }

   boost::asio::awaitable<forge::http::streaming_response> stream_object(object_get_request request) override {
      auto file = std::make_shared<std::ifstream>(object_path(request.collection, request.key), std::ios::binary);
      co_return forge::http::streaming_response::from_source(
         forge::http::streaming_response_options{
            .content_type = "application/octet-stream",
            .body =
               [file]() mutable -> boost::asio::awaitable<std::optional<forge::http::body_chunk>> {
                  auto bytes = std::vector<std::byte>(7);
                  file->read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                  const auto count = static_cast<std::size_t>(file->gcount());
                  if (count == 0) {
                     co_return std::nullopt;
                  }
                  bytes.resize(count);
                  co_return forge::http::body_chunk{.bytes = std::move(bytes)};
               },
         });
   }

   boost::asio::awaitable<forge::http::empty_response> delete_object(object_get_request request) override {
      std::filesystem::remove(object_path(request.collection, request.key));
      co_return forge::http::empty_response{.status_code = status::no_content};
   }

 private:
   [[nodiscard]] std::filesystem::path object_path(const std::string& collection, const std::string& key) const {
      return root_ / collection / key;
   }

   std::filesystem::path root_;
};

class object_proxy_api_impl final : public object_api {
 public:
   explicit object_proxy_api_impl(forge::api::handle<object_api> upstream) : upstream_{std::move(upstream)} {}

   boost::asio::awaitable<object_put_response> put_object(object_put_request request) override {
      co_return co_await upstream_->put_object(std::move(request));
   }

   boost::asio::awaitable<forge::http::file_response> get_object(object_get_request request) override {
      co_return co_await upstream_->get_object(std::move(request));
   }

   boost::asio::awaitable<forge::http::file_response> head_object(object_get_request request) override {
      co_return co_await upstream_->head_object(std::move(request));
   }

   boost::asio::awaitable<forge::http::streaming_response> stream_object(object_get_request request) override {
      co_return co_await upstream_->stream_object(std::move(request));
   }

   boost::asio::awaitable<forge::http::empty_response> delete_object(object_get_request request) override {
      co_return co_await upstream_->delete_object(std::move(request));
   }

 private:
   forge::api::handle<object_api> upstream_;
};

class json_stream_api_impl final : public json_stream_api {
 public:
   boost::asio::awaitable<forge::http::streaming_response> stream(json_stream_request request) override {
      auto chunks = std::make_shared<std::vector<std::string>>(
         std::vector<std::string>{request.id + ":", request.value});
      auto index = std::make_shared<std::size_t>(0);
      co_return forge::http::streaming_response::from_source(
         forge::http::streaming_response_options{
            .content_type = "text/plain",
            .body =
               [chunks, index]() mutable -> boost::asio::awaitable<std::optional<forge::http::body_chunk>> {
                  if (*index == chunks->size()) {
                     co_return std::nullopt;
                  }
                  const auto& text = (*chunks)[(*index)++];
                  auto bytes = std::vector<std::byte>(text.size());
                  std::memcpy(bytes.data(), text.data(), text.size());
                  co_return forge::http::body_chunk{.bytes = std::move(bytes)};
               },
         });
   }
};

class endpoint_api_impl final : public endpoint_api {
 public:
   explicit endpoint_api_impl(std::filesystem::path root) : root_{std::move(root)} {}

   boost::asio::awaitable<endpoint_control_response> current(endpoint_control_request request) override {
      request.response().set("X-Endpoint-Id", request.id);
      request.response().set_cookie("endpoint", request.id);
      const auto trace = request.request().header("X-Trace").value_or("missing-trace");
      co_return endpoint_control_response{
         .summary = request.id + ":" + std::string{request.request().target()} + ":" + std::string{trace},
      };
   }

   boost::asio::awaitable<forge::http::file_response> download(endpoint_control_request request) override {
      request.response().set("X-Endpoint-File", request.id);
      co_return forge::http::file_response::from_path(root_ / "asset.txt",
                                                    forge::http::file_options{.content_type = "text/plain"});
   }

   boost::asio::awaitable<forge::http::streaming_response> stream(endpoint_control_request request) override {
      request.response().set("X-Endpoint-Stream", request.id);
      request.response().set_cookie("endpoint", request.id);
      request.response().set_cookie("stream", "yes");
      auto text = std::make_shared<std::string>("stream:" + request.id);
      co_return forge::http::streaming_response::from_source(
         forge::http::streaming_response_options{
            .content_type = "text/plain",
            .body =
               [text, sent = false]() mutable -> boost::asio::awaitable<std::optional<forge::http::body_chunk>> {
                  if (sent) {
                     co_return std::nullopt;
                  }
                  sent = true;
                  auto bytes = std::vector<std::byte>(text->size());
                  std::memcpy(bytes.data(), text->data(), text->size());
                  co_return forge::http::body_chunk{.bytes = std::move(bytes)};
               },
         });
   }

   boost::asio::awaitable<forge::http::empty_response> accepted(endpoint_control_request request) override {
      request.response().set("X-Endpoint-Empty", request.id);
      co_return forge::http::empty_response{.status_code = status::accepted};
   }

 private:
   std::filesystem::path root_;
};

class stream_buffered_api_impl final : public stream_buffered_api {
 public:
   boost::asio::awaitable<endpoint_control_response> write(stream_buffered_request request) override {
      const auto payload = co_await request.body.async_read_all();
      request.response().set_cookie("endpoint", request.id);
      request.response().set_cookie("stream", payload);
      co_return endpoint_control_response{.summary = request.id + ":" + payload};
   }
};

class mixed_proxy_api_impl final : public mixed_proxy_api {
 public:
   explicit mixed_proxy_api_impl(std::filesystem::path root) : root_{std::move(root)} {}

   boost::asio::awaitable<control_response> read(std::string collection, std::string key) override {
      co_return control_response{.value = std::move(collection) + ":" + std::move(key)};
   }

   boost::asio::awaitable<forge::http::file_response> download(mixed_download_request request) override {
      co_return forge::http::file_response::from_path(
         root_ / request.collection / request.key,
         forge::http::file_options{.content_type = "application/octet-stream"});
   }

 private:
   std::filesystem::path root_;
};

class form_api_impl final : public form_api {
 public:
   boost::asio::awaitable<form_submit_response> submit(form_submit_request request) override {
      co_return form_submit_response{
         .summary = (request.label.present ? request.label.value : std::string{}) + ":" +
                    (request.count.present ? std::to_string(request.count.value) : std::string{"missing"}),
      };
   }
};

class control_api_impl final : public control_api {
 public:
   boost::asio::awaitable<forge::http::bytes_response> bytes(control_request request) override {
      auto text = std::string{"bytes:" + request.id};
      auto bytes = std::vector<std::byte>(text.size());
      std::memcpy(bytes.data(), text.data(), text.size());
      co_return forge::http::bytes_response{
         .bytes = std::move(bytes),
         .content_type = "application/control",
      };
   }

   boost::asio::awaitable<forge::http::empty_response> accepted(control_request) override {
      co_return forge::http::empty_response{.status_code = status::accepted};
   }

   boost::asio::awaitable<forge::http::empty_response> head(control_request) override {
      co_return forge::http::empty_response{.status_code = status::no_content};
   }
};

class alias_api_impl final : public alias_api {
 public:
   boost::asio::awaitable<control_response> current(control_request request) override {
      co_return control_response{.value = "current:" + request.id};
   }

   boost::asio::awaitable<control_response> legacy(control_request request) override {
      co_return control_response{.value = "legacy:" + request.id};
   }
};

class patch_api_impl final : public patch_api {
 public:
   boost::asio::awaitable<control_response> patch(control_patch_request request) override {
      co_return control_response{.value = request.id + ":" + request.value};
   }
};

class delete_body_api_impl final : public delete_body_api {
 public:
   boost::asio::awaitable<control_response> remove(delete_body_request request) override {
      const auto payload = request.payload.present ? request.payload.value.value : std::string{"missing"};
      co_return control_response{.value = request.ref + ":" + payload};
   }
};

class delete_path_api_impl final : public delete_path_api {
 public:
   boost::asio::awaitable<control_response> remove(delete_path_request request) override {
      co_return control_response{
         .value = request.collection + ":" + request.key + ":" +
                  (request.request().body().empty() ? "bodyless" : "body-present"),
      };
   }
};

class delete_stream_api_impl final : public delete_stream_api {
 public:
   boost::asio::awaitable<control_response> remove(delete_stream_request request) override {
      const auto payload = co_await request.body.async_read_all();
      co_return control_response{.value = request.ref + ":" + payload};
   }
};

std::uint16_t wait_for_port(const server& server) {
   for (auto attempt = 0; attempt != 100; ++attempt) {
      if (const auto port = server.port(); port != 0) {
         return port;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
   }
   throw std::runtime_error("http server did not bind a port in time");
}

request make_request(method verb, std::string target) {
   auto request_value = request{};
   request_value.method(verb);
   request_value.target(std::move(target));
   request_value.version(11);
   return request_value;
}

response make_json_response(const request& request, std::string body) {
   return make_text_response(request, status::ok, std::move(body), "application/json");
}

body_chunk make_body_chunk(std::string value) {
   auto bytes = std::vector<std::byte>(value.size());
   std::memcpy(bytes.data(), value.data(), value.size());
   return body_chunk{.bytes = std::move(bytes)};
}

class vector_body_source final : public body_reader::source {
 public:
   explicit vector_body_source(std::vector<std::string> chunks) : chunks_(std::move(chunks)) {}

   boost::asio::awaitable<std::optional<body_chunk>> async_read() override {
      if (index_ == chunks_.size()) {
         co_return std::nullopt;
      }
      auto chunk = make_body_chunk(chunks_[index_++]);
      bytes_read_ += chunk.bytes.size();
      co_return chunk;
   }

   [[nodiscard]] std::uint64_t bytes_read() const noexcept override {
      return bytes_read_;
   }

 private:
   std::vector<std::string> chunks_;
   std::size_t index_ = 0;
   std::uint64_t bytes_read_ = 0;
};

body_reader make_body_reader(std::vector<std::string> chunks) {
   return body_reader{std::make_shared<vector_body_source>(std::move(chunks))};
}

class temp_directory {
 public:
   temp_directory() {
      path_ = std::filesystem::temp_directory_path() /
              ("forge-http-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::filesystem::create_directories(path_);
   }

   ~temp_directory() {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
   }

   [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
   }

   void write(std::string_view name, std::string_view bytes) const {
      auto output = std::ofstream{path_ / std::string{name}, std::ios::binary};
      output << bytes;
   }

 private:
   std::filesystem::path path_;
};

response raw_http_exchange(std::uint16_t port, std::string request_text,
                           std::chrono::milliseconds hold_after_read = std::chrono::milliseconds{0}) {
   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});
   asio::write(stream.socket(), asio::buffer(request_text));

   auto buffer = beast::flat_buffer{};
   auto beast_response = beast_http::response<beast_http::string_body>{};
   beast_http::read(stream, buffer, beast_response);
   if (hold_after_read.count() != 0) {
      std::this_thread::sleep_for(hold_after_read);
   }
   return to_http_response(beast_response);
}

struct expect_continue_exchange_result {
   std::optional<response> interim;
   response final;
};

expect_continue_exchange_result raw_expect_continue_exchange(std::uint16_t port,
                                                             std::string headers,
                                                             std::string body,
                                                             std::chrono::milliseconds timeout =
                                                                std::chrono::seconds{2}) {
   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(timeout);
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});
   asio::write(stream.socket(), asio::buffer(headers));

   auto buffer = beast::flat_buffer{};
   auto first = beast_http::response<beast_http::string_body>{};
   stream.expires_after(timeout);
   beast_http::read(stream, buffer, first);
   auto first_response = to_http_response(first);
   if (first_response.result() != status::continue_) {
      return expect_continue_exchange_result{.interim = std::nullopt, .final = std::move(first_response)};
   }

   asio::write(stream.socket(), asio::buffer(body));
   auto second = beast_http::response<beast_http::string_body>{};
   stream.expires_after(timeout);
   beast_http::read(stream, buffer, second);
   return expect_continue_exchange_result{.interim = std::move(first_response), .final = to_http_response(second)};
}

std::pair<response, response>
raw_two_request_exchange(std::uint16_t port, std::string first_request, std::string second_request) {
   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});

   asio::write(stream.socket(), asio::buffer(first_request));
   auto buffer = beast::flat_buffer{};
   auto first = beast_http::response<beast_http::string_body>{};
   beast_http::read(stream, buffer, first);

   asio::write(stream.socket(), asio::buffer(second_request));
   auto second = beast_http::response<beast_http::string_body>{};
   beast_http::read(stream, buffer, second);
   return {to_http_response(first), to_http_response(second)};
}

response handle(router& target, route_context& context) {
   if (context.runtime != nullptr) {
      return forge::asio::blocking::run(*context.runtime, target.handle(context));
   }

   auto runtime = forge::asio::runtime{};
   context.runtime = &runtime;
   auto result = forge::asio::blocking::run(runtime, target.handle(context));
   context.runtime = nullptr;
   return result;
}

class tls_websocket_echo_server {
 public:
   tls_websocket_echo_server() : ssl_context_(asio::ssl::context::tls_server), acceptor_(io_context_) {
      ssl_context_.use_certificate_chain(asio::buffer(test_certificate()));
      ssl_context_.use_private_key(asio::buffer(test_private_key()), asio::ssl::context::pem);

      acceptor_.open(tcp::v4());
      acceptor_.set_option(asio::socket_base::reuse_address(true));
      acceptor_.bind(tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0});
      acceptor_.listen(asio::socket_base::max_listen_connections);
      port_ = acceptor_.local_endpoint().port();
      worker_ = std::thread([this] { run(); });
   }

   ~tls_websocket_echo_server() {
      auto ignored = boost::system::error_code{};
      acceptor_.close(ignored);
      io_context_.stop();
      if (worker_.joinable()) {
         worker_.join();
      }
   }

   [[nodiscard]] std::uint16_t port() const noexcept {
      return port_;
   }

 private:
   static std::string_view test_certificate() {
      return "-----BEGIN CERTIFICATE-----\n"
             "MIICpDCCAYwCCQCJjaEDxrQqBzANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
             "MjcuMC4wLjEwHhcNMjYwNDI5MDgwMTMzWhcNMjYwNDMwMDgwMTMzWjAUMRIwEAYD\n"
             "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDy\n"
             "sbPH/R4QUz725sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TX\n"
             "gl9tHkNpKmI92s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+\n"
             "x7MRWXfKYd/ArGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOS\n"
             "lI/lDqIjZxo7jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuv\n"
             "M+mTj6eO4UQ42w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXj\n"
             "nPOZzBinLRTDnE59HbDZAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHSOUQTEDgjC\n"
             "uwza9ayfThJTs43j+TziWHLlowqCiHt/ipRNFEW7L0ibTnbMdQBFGfaLkTAhc5Rd\n"
             "6O6x+9o76pgEYxEg0rDkgNXmprNmS+nL7Are+iiF6R+X8dts3MQgtONPApAXE96P\n"
             "/n5K4GDQTd3WCI37hkmJA6rmwziFDTlwqtKWts39g8PqAbXac27rVR/iD0gWdOws\n"
             "qiaoGj/0WW9qcgjYGdCc0/CbbnyiWbi48VVf0yyfm7wgcz90byaKIQchHdb/qjyU\n"
             "wy7nfU5TJ5MKQ5yeqPTWmPYZZp9TKa5VD6wZD/IH7jH3GdJ/fSyroVLZktVnmxJa\n"
             "dmG/9wwivwQ=\n"
             "-----END CERTIFICATE-----\n";
   }

   static std::string_view test_private_key() {
      return "-----BEGIN PRIVATE KEY-----\n"
             "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDysbPH/R4QUz72\n"
             "5sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TXgl9tHkNpKmI9\n"
             "2s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+x7MRWXfKYd/A\n"
             "rGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOSlI/lDqIjZxo7\n"
             "jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuvM+mTj6eO4UQ4\n"
             "2w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXjnPOZzBinLRTD\n"
             "nE59HbDZAgMBAAECggEBAIWVjHhy+V5RA+JRCh/12ayirNLG2BF30OP9pf7iL4IT\n"
             "/dMPbKvkmDGLw+1bW8tgKXj5+N6N/trfCm4zhqI3OF7ihooH9qYM88/F/OvMjFiU\n"
             "BhMVVhJW1LxtPPjKUcFN58M8VnMhRM9v6gIaoSOJZvpU1abVtgBDocyJUxAB6gYp\n"
             "i7MzoRwHGsL5mW/luE5H92/S8NNwLWBDA7DIGfrTZ6POf92h5I5W3CuTcqR5FICz\n"
             "3pfU3i443yZmsmkc9duH2gZ9cb9j4pRtNLbbsGmRVrBlgnkVFk8JWbikc8MpLeKO\n"
             "VKP7A2NvxJIrc7oFYrf4hbw8P70YL7S9B3W3yBPPzJECgYEA+Y3nG8CtvVTE/Keo\n"
             "qb5Rljlnj9DEffrylLyYUYfSSNR4Olc2WCPBiz0rPCDdO0VGeXAwqLf2VP7IEyAx\n"
             "kvrnqhzHWMhiLv+k4tIVyKCwpuofN0JsoUCi7CwRf+H2Pg+t6ewLV116THKsd41H\n"
             "IRElWyEvZsmbbhlLrsxUtfFZWnUCgYEA+PZwXUn+cb8kRmfG959gMawTtcfvnBUX\n"
             "sIn7LQl/ZWUIiLMWCaS3FbqkiGjaEYo6om1invYNJNA9zp/ECauSDp58NICCL0ie\n"
             "L7z26sEa6Ocg2VdR4ezpN3cM6dyAKfTFGb9V6qjyqNIPCE4eey6ZJ+CU/mpEfSDu\n"
             "+RGMzfdDCFUCgYEA5FRUn0zk6jU0YyMXq+9pgLSXL7vI/Kdt6m7AQuCto1tbga2o\n"
             "GG7mt/pIo6RCJufUemoO62AeL1hKQU2UbjHJYxkfv/jf9LaM68dijQWRe7b8xres\n"
             "4sFcEBCmFkbt4YzBCCWjntT1gBrv+Ba4fOXOMxoi374Yy1yzpYRpAWuI4L0CgYAn\n"
             "u1SlXrivuHx2i/tR62pzou2mVhkkRK16LBsczeY57UzWXBZJRbM+UYIOjwU2RWQk\n"
             "JebWTZg9ZspmXlLv5CS0FpDl5BhiqWktXy/cuSKtRq2UYf4cWy3A/0vdSqZdi8Wk\n"
             "3Uc94uaPEK77eVQd/orMtWexzo3NlmLs9uMMv8g/3QKBgQCbik0UoJkkqNRMmWG8\n"
             "dKQzj58eRI8fmKdJlWNfj2QMspd2vXMbsWYgAbFbU1QcVs1n8PxNydM+cfy77w8q\n"
             "NWMlYP7rUFQ3ekYWqrRlshZdJ/h24PALd1nPCvhc4C9dvn+zW3BLVez1lBuFO8n8\n"
             "0YkgmTgW7Ieibqnf4DqYp//nkw==\n"
             "-----END PRIVATE KEY-----\n";
   }

   void run() {
      try {
         auto socket = tcp::socket{io_context_};
         acceptor_.accept(socket);
         auto stream = beast::ssl_stream<beast::tcp_stream>{beast::tcp_stream{std::move(socket)}, ssl_context_};
         stream.handshake(asio::ssl::stream_base::server);
         auto websocket = beast_websocket::stream<beast::ssl_stream<beast::tcp_stream>>{std::move(stream)};
         websocket.accept();
         auto buffer = beast::flat_buffer{};
         websocket.read(buffer);
         websocket.text(websocket.got_text());
         websocket.write(buffer.data());
         websocket.close(beast_websocket::close_code::normal);
      } catch (...) {
      }
   }

   asio::io_context io_context_;
   asio::ssl::context ssl_context_;
   tcp::acceptor acceptor_;
   std::thread worker_;
   std::uint16_t port_ = 0;
};

class flaky_server {
 public:
   explicit flaky_server(bool respond_to_retry) : respond_to_retry_(respond_to_retry), acceptor_(io_context_) {
      acceptor_.open(tcp::v4());
      acceptor_.set_option(asio::socket_base::reuse_address(true));
      acceptor_.bind(tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0});
      acceptor_.listen(asio::socket_base::max_listen_connections);
      port_ = acceptor_.local_endpoint().port();
      worker_ = std::thread([this] { run(); });
   }

   ~flaky_server() {
      auto ignored = boost::system::error_code{};
      acceptor_.close(ignored);
      io_context_.stop();
      if (worker_.joinable()) {
         worker_.join();
      }
   }

   [[nodiscard]] std::uint16_t port() const noexcept {
      return port_;
   }

 private:
   void run() {
      try {
         auto first = tcp::socket{io_context_};
         acceptor_.accept(first);
         first.close();

         if (!respond_to_retry_) {
            return;
         }

         auto second = tcp::socket{io_context_};
         acceptor_.accept(second);
         auto stream = beast::tcp_stream{std::move(second)};
         auto buffer = beast::flat_buffer{};
         auto beast_request = beast_http::request<beast_http::string_body>{};
         beast_http::read(stream, buffer, beast_request);

         auto request_value = to_http_request(beast_request);
         auto response_value = make_text_response(request_value, status::ok, "retry-ok");
         response_value.keep_alive(false);
         auto beast_response = to_beast_response(response_value);
         beast_http::write(stream, beast_response);
         auto ignored = boost::system::error_code{};
         stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
      } catch (...) {
      }
   }

   bool respond_to_retry_ = false;
   asio::io_context io_context_;
   tcp::acceptor acceptor_;
   std::thread worker_;
   std::uint16_t port_ = 0;
};

BOOST_AUTO_TEST_CASE(http_request_response_copy_has_value_semantics) {
   auto original_request = make_request(method::post, "/items");
   original_request.set(field::content_type, "application/json");
   original_request.body() = R"({"ok":true})";
   auto copied_request = original_request;
   copied_request.target(std::string{"/other"});
   copied_request.set(field::content_type, "text/plain");
   copied_request.body() = "changed";

   BOOST_TEST(original_request.target() == "/items");
   BOOST_TEST(original_request[field::content_type] == "application/json");
   BOOST_TEST(original_request.body() == R"({"ok":true})");

   auto original_response = make_text_response(original_request, status::accepted, "accepted");
   auto copied_response = original_response;
   copied_response.result(status::bad_request);
   copied_response.set("X-Copy", "yes");
   copied_response.body() = "bad";

   BOOST_TEST(original_response.result() == status::accepted);
   const auto copy_header_absent = original_response.find("X-Copy") == original_response.end();
   BOOST_TEST(copy_header_absent);
   BOOST_TEST(original_response.body() == "accepted");
}

BOOST_AUTO_TEST_CASE(base_url_parses_https_origin_and_base_path) {
   const auto parsed = parse_base_url("https://node.example.com:9443/api");

   BOOST_TEST(parsed.secure());
   BOOST_TEST(parsed.origin() == "https://node.example.com:9443");
   BOOST_TEST(parsed.make_target("/v1/chain/get_info") == "/api/v1/chain/get_info");
}

BOOST_AUTO_TEST_CASE(target_parses_path_segments_and_query_params) {
   const auto parsed = parse_target("/items/42?expand=true&empty");

   BOOST_TEST(parsed.path == "/items/42");
   BOOST_REQUIRE_EQUAL(parsed.segments.size(), 2U);
   BOOST_TEST(parsed.segments[0] == "items");
   BOOST_TEST(parsed.segments[1] == "42");
   BOOST_TEST(parsed.query == "expand=true&empty");
   BOOST_REQUIRE_EQUAL(parsed.query_params.size(), 2U);
   BOOST_TEST(parsed.query_params[0].key == "expand");
   BOOST_TEST(parsed.query_params[0].value == "true");
   BOOST_TEST(parsed.query_params[0].has_value);
   BOOST_TEST(parsed.query_params[1].key == "empty");
   BOOST_TEST(!parsed.query_params[1].has_value);
}

BOOST_AUTO_TEST_CASE(router_does_not_expose_header_preflight_probe_api) {
   BOOST_TEST(!has_public_can_handle<router>);
   BOOST_TEST(!has_public_header_preflight_classifier<router>);
}

BOOST_AUTO_TEST_CASE(router_matches_static_and_parameter_routes) {
   auto router = forge::http::router{};
   router.get("/items/latest", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "static");
   });
   router.get("/items/:id", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, std::string{*context.route_param("id")});
   });

   auto static_request = make_request(method::get, "/items/latest?ignored=true");
   auto static_context = make_route_context(static_request);
   BOOST_TEST(handle(router, static_context).body() == "static");

   auto param_request = make_request(method::get, "/items/42");
   auto param_context = make_route_context(param_request);
   BOOST_TEST(handle(router, param_context).body() == "42");
}

BOOST_AUTO_TEST_CASE(router_returns_404_and_405) {
   auto router = forge::http::router{};
   router.get("/items/:id", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "ok");
   });

   auto missing_request = make_request(method::get, "/missing");
   auto missing_context = make_route_context(missing_request);
   BOOST_TEST(handle(router, missing_context).result_int() == static_cast<unsigned>(status::not_found));

   auto wrong_method_request = make_request(method::post, "/items/42");
   auto wrong_method_context = make_route_context(wrong_method_request);
   BOOST_TEST(handle(router, wrong_method_context).result_int() == static_cast<unsigned>(status::method_not_allowed));
}

BOOST_AUTO_TEST_CASE(router_awaits_async_route_handler) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   router.get("/async", [invoked](route_context& context) -> boost::asio::awaitable<response> {
      auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
      timer.expires_after(std::chrono::milliseconds{1});
      co_await timer.async_wait(boost::asio::use_awaitable);
      invoked->store(true);
      co_return make_text_response(context.request, status::ok, "async-ok");
   });

   auto request = make_request(method::get, "/async");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = forge::asio::blocking::run(runtime, router.handle(context));
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "async-ok");
   BOOST_TEST(invoked->load());
}

BOOST_AUTO_TEST_CASE(router_awaits_async_middleware_chain) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   auto trace = std::make_shared<std::string>();

   router.use([trace](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(context);
      *trace += "before>";
      auto response = co_await next();
      *trace += "<after";
      co_return response;
   });
   router.get("/ok", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "ok");
   });

   auto request = make_request(method::get, "/ok");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = forge::asio::blocking::run(runtime, router.handle(context));
   BOOST_TEST(response.body() == "ok");
   BOOST_TEST(*trace == "before><after");
}

BOOST_AUTO_TEST_CASE(router_maps_typed_http_exception_to_native_json_response) {
   auto router = forge::http::router{};
   router.get("/missing", [](route_context&) -> boost::asio::awaitable<response> {
      FORGE_THROW_EXCEPTION(forge::http::exceptions::not_found, "chunk not found");
   });

   auto request = make_request(method::get, "/missing");
   auto context = make_route_context(request);
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find(R"("error":"not_found")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("category":"forge.http")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("code":404)") != std::string::npos);
   BOOST_TEST(response.body().find("chunk not found") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(router_escapes_control_bytes_in_exception_json) {
   auto router = forge::http::router{};
   router.get("/bad", [](route_context&) -> boost::asio::awaitable<response> {
      auto message = std::string{"invalid"};
      message.push_back('\x01');
      message.push_back('\b');
      message += "field";
      FORGE_THROW_EXCEPTION(forge::http::exceptions::bad_request, message);
   });

   auto request = make_request(method::get, "/bad");
   auto context = make_route_context(request);
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::bad_request));
   BOOST_TEST(response[field::content_type] == "application/json");
   const auto parsed = forge::json::read_value(response.body());
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.get_object()["error"].get_string() == "bad_request");
   auto contains_raw_control = false;
   for (const auto character : response.body()) {
      const auto byte = static_cast<unsigned char>(character);
      contains_raw_control = contains_raw_control || (byte < 0x20U);
   }
   BOOST_TEST(!contains_raw_control);
   BOOST_TEST(response.body().find("\\u0000") != std::string::npos);
   const auto escaped_backspace =
       response.body().find("\\b") != std::string::npos || response.body().find("\\u0008") != std::string::npos;
   BOOST_TEST(escaped_backspace);
}

BOOST_AUTO_TEST_CASE(router_rejects_duplicate_routes_before_serving) {
   auto router = forge::http::router{};
   router.get("/items", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "one");
   });

   BOOST_CHECK_THROW(
       router.get("/items",
                  [](route_context& context) -> boost::asio::awaitable<response> {
                     co_return make_text_response(context.request, status::ok, "two");
                  }),
       forge::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(router_rejects_duplicate_buffered_and_stream_routes) {
   auto router = forge::http::router{};
   router.get("/items", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "buffered");
   });

   BOOST_CHECK_THROW(
      router.get_stream("/items",
                        [](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
                           co_return stream_response::buffered(
                              make_text_response(request_value.context.request, status::ok, "stream"));
                        }),
      forge::http::exceptions::conflict);

   auto reverse = forge::http::router{};
   reverse.post_stream("/upload", [](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return stream_response::buffered(make_text_response(request_value.context.request, status::ok, "stream"));
   });

   BOOST_CHECK_THROW(
      reverse.post("/upload",
                   [](route_context& context) -> boost::asio::awaitable<response> {
                      co_return make_text_response(context.request, status::ok, "buffered");
                   }),
      forge::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(http_api_plan_maps_custom_exception_to_native_status) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<throwing_api_cache>());

   auto router = forge::http::router{};
   auto plan_builder = forge::api::binding();
   plan_builder.serve(apis);
   auto builder = forge::http::api::binding(router);
   builder.use(std::move(plan_builder).build());
   builder.get<&api_cache::read, api_read_chunk, api_chunk>("/cache/chunks/:ref");
   auto binding = std::move(builder).build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find(R"("error":"chunk_not_found")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("category":"test.http.cache")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("code":1)") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_plan_populates_get_request_from_route_and_query) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<routed_api_cache>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding()
                      .use(forge::api::binding().serve(apis).build())
                      .get<&api_cache::routed_read, api_routed_read_chunk, api_chunk>(
                          "/cache/chunks/:ref",
                          {.query = {{.field = "offset", .name = "offset"}, {.field = "limit", .name = "limit"}}})
                      .build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc?offset=7&limit=4096");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto unpacked = forge::json::read<api_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(unpacked.ok());
   BOOST_TEST(unpacked.value.bytes == "abc:7:4096");
}

BOOST_AUTO_TEST_CASE(http_api_plan_escapes_json_error_fields) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<escaping_api_cache>());

   auto router = forge::http::router{};
   auto binding =
       forge::http::api::binding().use(forge::api::binding().serve(apis).build())
           .get<&api_cache::read, api_read_chunk, api_chunk>("/cache/chunks/:ref")
           .build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   const auto parsed = forge::json::read_value(response.body());
   BOOST_REQUIRE(parsed.ok());
   const auto& message = parsed.value.get_object()["message"].get_string();
   BOOST_TEST(message.find("chunk \"missing\"") != std::string::npos);
   BOOST_TEST(message.find("not found") != std::string::npos);
   auto contains_raw_control = false;
   for (const auto character : response.body()) {
      contains_raw_control = contains_raw_control || static_cast<unsigned char>(character) < 0x20U;
   }
   BOOST_TEST(!contains_raw_control);
}

BOOST_AUTO_TEST_CASE(http_api_plan_passes_put_body_to_typed_api) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<throwing_api_cache>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding()
                      .use(forge::api::binding().serve(apis).build())
                      .put<&api_cache::write, api_chunk, api_chunk>("/cache/chunks/:ref")
                      .build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   request.set(field::content_type, "application/json");
   request.body() = R"({"bytes":"from-put-body"})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto unpacked = forge::json::read<api_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(unpacked.ok());
   BOOST_TEST(unpacked.value.bytes == "from-put-body");
}

BOOST_AUTO_TEST_CASE(http_api_macro_describes_routes_for_forge_api) {
   const auto routes = forge::http::api::traits<macro_cache>::routes();

   BOOST_REQUIRE_EQUAL(routes.size(), 2U);
   BOOST_TEST(routes[0].verb == method::get);
   BOOST_TEST(routes[0].method_name == "read");
   BOOST_TEST(routes[0].target == "/cache/chunks/:ref?offset={offset}&limit={limit}");
   BOOST_TEST(routes[0].success_status == status::ok);
   BOOST_TEST(routes[1].verb == method::put);
   BOOST_TEST(routes[1].method_name == "write");
   BOOST_TEST(routes[1].target == "/cache/chunks/:ref");
   BOOST_TEST(routes[1].success_status == status::created);
}

BOOST_AUTO_TEST_CASE(http_api_macro_get_maps_route_and_query) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc?offset=7&limit=4096");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto unpacked = forge::json::read<macro_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(unpacked.ok());
   BOOST_TEST(unpacked.value.bytes == "abc:7:4096");
}

BOOST_AUTO_TEST_CASE(http_api_query_template_preserves_wire_alias) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<search_api>(search_api::describe(), std::make_shared<search_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<search_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto request_value = make_request(method::get, "/search/cache?page_size=25");
   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));
   const auto decoded = forge::json::read<search_response>(response.body());
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.value == "cache:25");

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto search = forge::asio::blocking::run(runtime, forge::http::api::remote<search_api>(client));
   const auto remote_response =
      forge::asio::blocking::run(runtime, search->search(search_request{.term = "remote", .limit = 17}));
   BOOST_TEST(remote_response.value == "remote:17");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_typed_proxy_sends_default_mapped_header_fields) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<default_header_api>(default_header_api::describe(), std::make_shared<default_header_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<default_header_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto headers = forge::asio::blocking::run(runtime, forge::http::api::remote<default_header_api>(client));

   const auto response = forge::asio::blocking::run(
      runtime,
      headers->echo(default_header_request{
         .request_id = forge::http::header<std::string>{.value = "trace-123", .present = true},
         .body = forge::http::body_stream{make_body_reader({"payload"})},
      }));

   BOOST_TEST(response.present);
   BOOST_TEST(response.request_id == "trace-123");
   BOOST_TEST(response.body == "payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_dto_parameters_bind_query_header_cookie_and_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<dto_http_api>(dto_http_api::describe(), std::make_shared<dto_http_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<dto_http_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<dto_http_api>(client));

   const auto response = forge::asio::blocking::run(
      runtime,
      api->write(dto_http_request{
         .ref = "chunk-1",
         .limit = forge::http::query<std::uint32_t>{.value = 9, .present = true},
         .request_id = forge::http::header<std::string>{.value = "trace-123", .present = true},
         .session = forge::http::cookie<std::string>{.value = "session-7", .present = true},
         .payload =
            forge::http::body<positional_body_payload>{
               .value = positional_body_payload{.value = "payload"},
               .present = true,
            },
      }));

   BOOST_TEST(response.summary == "chunk-1:9:trace-123:session-7:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_typed_proxy_sends_delete_json_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<delete_body_api>(delete_body_api::describe(), std::make_shared<delete_body_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<delete_body_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<delete_body_api>(client));

   const auto response = forge::asio::blocking::run(
      runtime,
      api->remove(delete_body_request{
         .ref = "chunk-1",
         .payload =
            forge::http::body<positional_body_payload>{
               .value = positional_body_payload{.value = "payload"},
               .present = true,
            },
      }));

   BOOST_TEST(response.value == "chunk-1:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_typed_proxy_keeps_path_only_delete_bodyless) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<delete_path_api>(delete_path_api::describe(), std::make_shared<delete_path_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<delete_path_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<delete_path_api>(client));

   auto request = delete_path_request{};
   request.collection = "cache";
   request.key = "chunk-1";
   const auto response = forge::asio::blocking::run(
      runtime,
      api->remove(std::move(request)));

   BOOST_TEST(response.value == "cache:chunk-1:bodyless");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_delete_stream_body_route_mounts_and_reads_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<delete_stream_api>(delete_stream_api::describe(), std::make_shared<delete_stream_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<delete_stream_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<delete_stream_api>(client));

   auto response = forge::asio::blocking::run(
      runtime,
      api->remove(delete_stream_request{
         .ref = "chunk-stream",
         .body = forge::http::body_stream{make_body_reader({"pay", "load"})},
      }));

   BOOST_TEST(response.value == "chunk-stream:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_endpoint_request_injects_request_and_response_state) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto directory = temp_directory{};
   directory.write("asset.txt", "file-body");

   auto apis = forge::api::registry{};
   apis.install<endpoint_api>(endpoint_api::describe(), std::make_shared<endpoint_api_impl>(directory.path()));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<endpoint_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto connection = forge::http::connection{
      runtime,
      parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
   };

   auto current_request = make_request(method::get, "/endpoint/abc");
   current_request.set("X-Trace", "trace-1");
   auto current = forge::asio::blocking::run(runtime, connection.async_request(std::move(current_request)));
   auto decoded = forge::json::read<endpoint_control_response>(current.body());
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.summary == "abc:/endpoint/abc:trace-1");
   BOOST_TEST(current["X-Endpoint-Id"] == "abc");
   BOOST_TEST(current["Set-Cookie"].find("endpoint=abc") != std::string::npos);

   auto file = forge::asio::blocking::run(runtime, connection.async_request(make_request(method::get, "/endpoint/abc/file")));
   BOOST_TEST(file.body() == "file-body");
   BOOST_TEST(file["X-Endpoint-File"] == "abc");

   auto stream = forge::asio::blocking::run(runtime, connection.async_request(make_request(method::get, "/endpoint/abc/stream")));
   BOOST_TEST(stream.body() == "stream:abc");
   BOOST_TEST(stream["X-Endpoint-Stream"] == "abc");
   const auto stream_headers = stream.headers();
   const auto stream_cookies = std::count_if(stream_headers.begin(), stream_headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie";
   });
   BOOST_TEST(stream_cookies == 2);
   BOOST_TEST(std::any_of(stream_headers.begin(), stream_headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie" && header.text == "endpoint=abc";
   }));
   BOOST_TEST(std::any_of(stream_headers.begin(), stream_headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie" && header.text == "stream=yes";
   }));

   auto accepted =
      forge::asio::blocking::run(runtime, connection.async_request(make_request(method::get, "/endpoint/abc/accepted")));
   BOOST_TEST(accepted.result() == status::accepted);
   BOOST_TEST(accepted["X-Endpoint-Empty"] == "abc");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_endpoint_request_preserves_repeated_set_cookie_on_stream_response) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto directory = temp_directory{};

   auto apis = forge::api::registry{};
   apis.install<endpoint_api>(endpoint_api::describe(), std::make_shared<endpoint_api_impl>(directory.path()));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<endpoint_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto connection = forge::http::connection{
      runtime,
      parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
   };
   auto stream = forge::asio::blocking::run(runtime, connection.async_request(make_request(method::get, "/endpoint/abc/stream")));
   const auto headers = stream.headers();

   BOOST_TEST(std::count_if(headers.begin(), headers.end(), [](const header_entry& header) {
                 return header.name == "Set-Cookie";
              }) == 2);
   BOOST_TEST(std::any_of(headers.begin(), headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie" && header.text == "endpoint=abc";
   }));
   BOOST_TEST(std::any_of(headers.begin(), headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie" && header.text == "stream=yes";
   }));

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_stream_path_buffered_response_merges_endpoint_headers_once) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<stream_buffered_api>(stream_buffered_api::describe(), std::make_shared<stream_buffered_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<stream_buffered_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto connection = forge::http::connection{
      runtime,
      parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
   };
   auto request = make_request(method::put, "/stream-buffered/abc");
   request.body() = "payload";
   request.set(field::content_type, "application/octet-stream");
   request.prepare_payload();

   auto response_value = forge::asio::blocking::run(runtime, connection.async_request(std::move(request)));
   const auto headers = response_value.headers();

   BOOST_TEST(std::count_if(headers.begin(), headers.end(), [](const header_entry& header) {
                 return header.name == "Set-Cookie";
              }) == 2);
   BOOST_TEST(std::any_of(headers.begin(), headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie" && header.text == "endpoint=abc";
   }));
   BOOST_TEST(std::any_of(headers.begin(), headers.end(), [](const header_entry& header) {
      return header.name == "Set-Cookie" && header.text == "stream=payload";
   }));

   auto decoded = forge::json::read<endpoint_control_response>(response_value.body());
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.summary == "abc:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_fallback_proxy_supports_positional_methods_in_mixed_api) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto directory = temp_directory{};
   directory.write("catalog/alpha", "file-body");

   auto apis = forge::api::registry{};
   apis.install<mixed_proxy_api>(mixed_proxy_api::describe(), std::make_shared<mixed_proxy_api_impl>(directory.path()));

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<mixed_proxy_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<mixed_proxy_api>(client));

   const auto response_value = forge::asio::blocking::run(runtime, api->read("catalog", "alpha"));
   BOOST_TEST(response_value.value == "catalog:alpha");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_dto_body_bytes_roundtrips) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<dto_http_api>(dto_http_api::describe(), std::make_shared<dto_http_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<dto_http_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<dto_http_api>(client));

   const auto text = std::string{"raw-payload"};
   auto bytes = std::vector<std::byte>(text.size());
   std::memcpy(bytes.data(), text.data(), text.size());
   const auto response = forge::asio::blocking::run(
      runtime,
      api->write_bytes(dto_bytes_request{
         .ref = "chunk-bytes",
         .bytes = forge::http::body_bytes{.bytes = std::move(bytes)},
      }));

   BOOST_TEST(response.summary == "chunk-bytes:raw-payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_dto_multipart_form_and_upload_roundtrips) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<dto_http_api>(dto_http_api::describe(), std::make_shared<dto_http_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<dto_http_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto file_text = std::string{"file-content"};
   auto file_bytes = std::vector<std::byte>(file_text.size());
   std::memcpy(file_bytes.data(), file_text.data(), file_text.size());
   auto part = forge::http::upload_part{
      .name = "file",
      .filename = "demo.txt",
      .content_type = "text/plain",
      .memory = std::move(file_bytes),
      .size = static_cast<std::uint64_t>(file_text.size()),
   };

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<dto_http_api>(client));

   const auto response = forge::asio::blocking::run(
      runtime,
      api->upload(dto_multipart_request{
         .category = forge::http::form<std::string>{.value = "images", .present = true},
         .count = forge::http::form_field<std::uint32_t>{.value = 2, .present = true},
         .file = forge::http::upload_file{std::move(part)},
      }));

   BOOST_TEST(response.summary == "images:2:demo.txt:file-content");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_dto_body_field_reports_schema_errors) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<dto_http_api>(dto_http_api::describe(), std::make_shared<dto_http_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<dto_http_api>().build();
   router.mount(binding);

   auto request = make_request(method::post, "/dto/chunk-invalid?limit=7");
   request.set(field::content_type, "application/json");
   request.body() = R"({"value":""})";
   request.prepare_payload();
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response.body().find("validation_error") != std::string::npos);
   BOOST_TEST(response.body().find("schema.non_empty") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_dto_rejects_multiple_body_sources) {
   auto runtime = forge::asio::runtime{};
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:9")};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<dto_ambiguous_body_api>(client));

   const auto text = std::string{"raw-payload"};
   auto bytes = std::vector<std::byte>(text.size());
   std::memcpy(bytes.data(), text.data(), text.size());

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         api->write(dto_ambiguous_body_request{
            .ref = "chunk-1",
            .payload =
               forge::http::body<positional_body_payload>{
                  .value = positional_body_payload{.value = "json-payload"},
                  .present = true,
               },
            .bytes = forge::http::body_bytes{.bytes = std::move(bytes)},
         })),
      forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_api_rejects_http_parameter_wrappers) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_http_api>(positional_http_api::describe(), std::make_shared<positional_http_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_http_api>().build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_body_wrapper_is_rejected) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_body_api>(positional_body_api::describe(), std::make_shared<positional_body_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_body_api>().build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_stream_wrapper_is_rejected) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_stream_api>(positional_stream_api::describe(),
                                       std::make_shared<positional_stream_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_stream_api>().build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_client_rejects_http_parameter_wrappers) {
   auto runtime = forge::asio::runtime{};
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:9")};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<positional_stream_api>(client));

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         api->write("chunk-3", forge::http::body_stream{make_body_reader({"payload"})})),
      forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_unconsumed_scalar_body_is_rejected) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_scalar_body_api>(positional_scalar_body_api::describe(),
                                            std::make_shared<positional_scalar_body_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_scalar_body_api>().build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_ordinary_dto_body_roundtrips_without_body_wrapper) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<positional_plain_body_api>(positional_plain_body_api::describe(),
                                           std::make_shared<positional_plain_body_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_plain_body_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<positional_plain_body_api>(client));

   const auto response = forge::asio::blocking::run(
      runtime,
      api->write("chunk-plain", positional_body_payload{.value = "payload"}));

   BOOST_TEST(response.summary == "chunk-plain:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_positional_stream_response_decodes_ordinary_dto_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<positional_streaming_body_api>(positional_streaming_body_api::describe(),
                                               std::make_shared<positional_streaming_body_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_streaming_body_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto api = forge::asio::blocking::run(runtime, forge::http::api::remote<positional_streaming_body_api>(client));

   auto response = forge::asio::blocking::run(
      runtime,
      api->write("chunk-stream", positional_body_payload{.value = "payload"}));
   auto text = forge::asio::blocking::run(runtime, response.body().async_read_all());

   BOOST_TEST(response.status_code() == status::ok);
   BOOST_TEST(response.content_type() == "text/plain");
   BOOST_TEST(text == "chunk-stream:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_positional_ordinary_dto_body_rejects_multiple_candidates) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_ambiguous_body_api>(positional_ambiguous_body_api::describe(),
                                               std::make_shared<positional_ambiguous_body_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_ambiguous_body_api>().build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_positional_ordinary_dto_body_reports_schema_errors) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_plain_body_api>(positional_plain_body_api::describe(),
                                           std::make_shared<positional_plain_body_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_plain_body_api>().build();
   router.mount(binding);

   auto request = make_request(method::post, "/plain/chunk-invalid");
   request.set(field::content_type, "application/json");
   request.body() = R"({"value":""})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response.body().find("validation_error") != std::string::npos);
   BOOST_TEST(response.body().find("schema.non_empty") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_positional_ordinary_dto_body_rejects_route_disagreement) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<positional_checked_body_api>(positional_checked_body_api::describe(),
                                             std::make_shared<positional_checked_body_api_impl>());

   auto router = forge::http::router{};
   auto binding =
      forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<positional_checked_body_api>().build();
   router.mount(binding);

   auto request = make_request(method::post, "/checked/route-ref");
   request.set(field::content_type, "application/json");
   request.body() = R"({"ref":"body-ref","value":"payload"})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response.body().find("validation_error") != std::string::npos);
   BOOST_TEST(response.body().find("disagrees") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_parameter_wrappers_reject_generic_raw_serialization) {
   BOOST_CHECK_THROW(
      (void)forge::api::pack_body(forge::http::query<std::uint32_t>{.value = 7, .present = true}),
      forge::api::exceptions::protocol_error);
   BOOST_CHECK_THROW(
      (void)forge::api::pack_body(forge::http::header<std::string>{.value = "trace", .present = true}),
      forge::api::exceptions::protocol_error);
   BOOST_CHECK_THROW(
      (void)forge::api::pack_body(forge::http::body<positional_body_payload>{
         .value = positional_body_payload{.value = "payload"},
         .present = true,
      }),
      forge::api::exceptions::protocol_error);
}

BOOST_AUTO_TEST_CASE(http_api_validates_schema_after_route_and_query_binding) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<search_api>(search_api::describe(), std::make_shared<search_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<search_api>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/search/cache?page_size=0");
   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find("validation_error") != std::string::npos);
   BOOST_TEST(response.body().find("schema.range") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_macro_put_rejects_body_route_disagreement) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   request.set(field::content_type, "application/json");
   request.body() = R"({"ref":"other","bytes":"payload"})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response.body().find("disagrees") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_macro_put_rejects_unsupported_media_type) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   request.set(field::content_type, "application/octet-stream");
   request.body() = "payload";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::unsupported_media_type));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find("unsupported_media_type") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_macro_put_uses_default_json_codec_without_content_type) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   request.body() = R"({"ref":"abc","bytes":"payload"})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::json::read<macro_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::created));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.bytes == "abc:payload");
}

BOOST_AUTO_TEST_CASE(http_api_macro_put_reports_invalid_json_as_validation_error) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   request.set(field::content_type, "application/json");
   request.body() = R"({"ref":"abc","bytes":)";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find("validation_error") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_xml_request_and_response_body_roundtrip) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/xml/cache/chunks/abc");
   request.set(field::content_type, "application/xml; charset=utf-8");
   request.set("Accept", "application/xml");
   request.body() = "<macro_write_request><ref>abc</ref><bytes>payload</bytes></macro_write_request>";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read<macro_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::created));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.bytes == "abc:payload");
}

BOOST_AUTO_TEST_CASE(http_api_xml_error_body_uses_route_error_codec) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/xml/cache/chunks/abc");
   request.set(field::content_type, "application/json");
   request.body() = R"({"ref":"abc","bytes":"payload"})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read_value(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::unsupported_media_type));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(xml_child_text(decoded.value.root, "error") == "unsupported_media_type");
}

BOOST_AUTO_TEST_CASE(http_api_request_content_type_rejects_wildcard_media_range) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/xml/cache/chunks/abc");
   request.set(field::content_type, "application/*");
   request.body() = "<macro_write_request><ref>abc</ref><bytes>payload</bytes></macro_write_request>";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read_value(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::unsupported_media_type));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(xml_child_text(decoded.value.root, "error") == "unsupported_media_type");
}

BOOST_AUTO_TEST_CASE(http_api_xml_route_rejects_unacceptable_accept) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/xml/cache/chunks/abc?offset=1&limit=2");
   request.set("Accept", "application/json");

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read_value(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(xml_child_text(decoded.value.root, "error") == "not_acceptable");
}

BOOST_AUTO_TEST_CASE(http_api_xml_route_rejects_specific_zero_quality_accept) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/xml/cache/chunks/abc?offset=1&limit=2");
   request.set("Accept", "application/xml;q=0, */*;q=1");

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read_value(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(xml_child_text(decoded.value.root, "error") == "not_acceptable");
}

BOOST_AUTO_TEST_CASE(http_api_xml_route_combines_repeated_accept_headers) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/xml/cache/chunks/abc?offset=1&limit=2");
   request.set("Accept", "text/plain");
   request.insert("Accept", "application/xml");

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read<macro_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.bytes == "abc:1:2");
}

BOOST_AUTO_TEST_CASE(http_api_xml_route_combines_repeated_accept_zero_quality_before_wildcard) {
   auto runtime = forge::asio::runtime{};
   auto writes = std::make_shared<std::atomic<std::uint32_t>>(0U);
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>(writes));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/xml/cache/chunks/abc");
   request.set(field::content_type, "application/xml");
   request.set("Accept", "application/xml;q=0");
   request.insert("Accept", "*/*;q=1");
   request.body() = "<macro_write_request><ref>abc</ref><bytes>payload</bytes></macro_write_request>";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read_value(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(xml_child_text(decoded.value.root, "error") == "not_acceptable");
   BOOST_TEST(writes->load(std::memory_order_relaxed) == 0U);
}

BOOST_AUTO_TEST_CASE(http_api_xml_route_negotiates_actual_response_media_type_before_handler_invocation) {
   auto runtime = forge::asio::runtime{};
   auto writes = std::make_shared<std::atomic<std::uint32_t>>(0U);
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>(writes));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   const auto exercise = [&](std::string_view accept) {
      auto request = make_request(method::put, "/xml/cache/chunks/abc");
      request.set(field::content_type, "application/xml");
      request.set("Accept", accept);
      request.body() = "<macro_write_request><ref>abc</ref><bytes>payload</bytes></macro_write_request>";
      request.prepare_payload();

      auto context = make_route_context(request);
      context.runtime = &runtime;

      const auto response = handle(router, context);
      const auto decoded = forge::xml::read_value(response.body());

      BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
      BOOST_TEST(response[field::content_type] == "application/xml");
      BOOST_REQUIRE(decoded.ok());
      BOOST_TEST(xml_child_text(decoded.value.root, "error") == "not_acceptable");
   };

   exercise("text/xml;q=1, application/xml;q=0");
   exercise("application/problem+xml");
   exercise("application/xml;profile=foo;q=1, application/xml;q=0");
   BOOST_TEST(writes->load(std::memory_order_relaxed) == 0U);
}

BOOST_AUTO_TEST_CASE(http_api_json_route_rejects_specific_zero_quality_accept) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc?offset=1&limit=2");
   request.set("Accept", "application/json;q=0, */*;q=1");

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find("not_acceptable") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_route_rejects_specific_family_zero_quality_accept) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/xml/cache/chunks/abc?offset=1&limit=2");
   request.set("Accept", "application/*;q=0, */*;q=1");

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
   BOOST_TEST(response[field::content_type] == "application/xml");
}

BOOST_AUTO_TEST_CASE(http_api_rejects_unacceptable_accept_before_handler_invocation) {
   auto runtime = forge::asio::runtime{};
   auto writes = std::make_shared<std::atomic<std::uint32_t>>(0U);
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>(writes));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/xml/cache/chunks/abc");
   request.set(field::content_type, "application/xml");
   request.set("Accept", "application/json");
   request.body() = "<macro_write_request><ref>abc</ref><bytes>payload</bytes></macro_write_request>";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto decoded = forge::xml::read_value(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_acceptable));
   BOOST_TEST(response[field::content_type] == "application/xml");
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(xml_child_text(decoded.value.root, "error") == "not_acceptable");
   BOOST_TEST(writes->load(std::memory_order_relaxed) == 0U);
}

BOOST_AUTO_TEST_CASE(typed_http_client_supports_xml_request_and_response_bodies) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<xml_cache_api>(xml_cache_api::describe(), std::make_shared<xml_cache_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<xml_cache_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto cache = forge::asio::blocking::run(runtime, forge::http::api::remote<xml_cache_api>(client));

   const auto response =
      forge::asio::blocking::run(runtime, cache->write(macro_write_request{.ref = "abc", .bytes = "payload"}));

   BOOST_TEST(response.bytes == "abc:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_api_native_responses_bypass_xml_codec_options) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto files = temp_directory{};
   std::filesystem::create_directories(files.path() / "cache");
   files.write("cache/chunk.bin", "file-payload");

   auto apis = forge::api::registry{};
   apis.install<control_api>(control_api::describe(), std::make_shared<control_api_impl>());
   apis.install<object_api>(object_api::describe(), std::make_shared<object_api_impl>(files.path()));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding()
                     .use(forge::api::binding().serve(apis).build())
                     .route<&control_api::bytes, control_request, forge::http::bytes_response>(
                        forge::http::api::route_builder{method::get, "bytes", "/xml/native/:id/bytes", status::ok}
                           .response_body_codec(forge::http::api::body_codec::xml)
                           .build())
                     .route<&control_api::accepted, control_request, forge::http::empty_response>(
                        forge::http::api::route_builder{method::get, "accepted", "/xml/native/:id/accepted", status::ok}
                           .response_body_codec(forge::http::api::body_codec::xml)
                           .build())
                     .route<&object_api::get_object, object_get_request, forge::http::file_response>(
                        forge::http::api::route_builder{method::get,
                                                        "get_object",
                                                        "/xml/native/:collection/:key/file",
                                                        status::ok}
                           .response_file()
                           .response_body_codec(forge::http::api::body_codec::xml)
                           .build())
                     .route<&object_api::stream_object, object_get_request, forge::http::streaming_response>(
                        forge::http::api::route_builder{method::get,
                                                        "stream_object",
                                                        "/xml/native/:collection/:key/stream",
                                                        status::ok}
                           .response_stream()
                           .response_body_codec(forge::http::api::body_codec::xml)
                           .build())
                     .build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};

   const auto bytes = forge::asio::blocking::run(runtime, client.async_get("/xml/native/abc/bytes"));
   BOOST_TEST(bytes.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(bytes[field::content_type] == "application/control");
   BOOST_TEST(bytes.body() == "bytes:abc");

   const auto empty = forge::asio::blocking::run(runtime, client.async_get("/xml/native/abc/accepted"));
   BOOST_TEST(empty.result_int() == static_cast<unsigned>(status::accepted));
   BOOST_TEST(empty.body().empty());

   const auto file = forge::asio::blocking::run(runtime, client.async_get("/xml/native/cache/chunk.bin/file"));
   BOOST_TEST(file.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(file[field::content_type] == "application/octet-stream");
   BOOST_TEST(file.body() == "file-payload");

   const auto stream = forge::asio::blocking::run(runtime, client.async_get("/xml/native/cache/chunk.bin/stream"));
   BOOST_TEST(stream.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(stream[field::content_type] == "application/octet-stream");
   BOOST_TEST(stream.body() == "file-payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(typed_http_client_supports_handle_methods) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto cache = forge::asio::blocking::run(runtime, forge::http::api::remote<macro_cache>(client));
   auto chunk = forge::asio::blocking::run(
      runtime, cache->read(macro_read_request{.ref = "abc", .offset = 3, .limit = 64}));
   auto receipt = forge::asio::blocking::run(
      runtime, cache->write(macro_write_request{.ref = "abc", .bytes = "payload"}));

   BOOST_TEST(chunk.bytes == "abc:3:64");
   BOOST_TEST(receipt.bytes == "abc:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(typed_http_client_supports_native_bytes_and_empty_responses) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<control_api>(control_api::describe(), std::make_shared<control_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<control_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto control = forge::asio::blocking::run(runtime, forge::http::api::remote<control_api>(client));

   auto bytes = forge::asio::blocking::run(runtime, control->bytes(control_request{.id = "abc"}));
   auto text = std::string(bytes.bytes.size(), '\0');
   std::memcpy(text.data(), bytes.bytes.data(), bytes.bytes.size());
   BOOST_TEST(bytes.status_code == status::ok);
   BOOST_TEST(bytes.content_type == "application/control");
   BOOST_TEST(text == "bytes:abc");

   auto head = forge::asio::blocking::run(runtime, control->head(control_request{.id = "abc"}));
   BOOST_TEST(head.status_code == status::no_content);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_empty_response_frames_keep_alive_body_capable_status) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<control_api>(control_api::describe(), std::make_shared<control_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<control_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), server.port()});

   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /controls/abc/accepted HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
      "\r\n"}));

   auto buffer = beast::flat_buffer{};
   auto first_parser = beast_http::response_parser<beast_http::string_body>{};
   first_parser.skip(true);
   auto read_error = boost::system::error_code{};
   stream.expires_after(std::chrono::milliseconds{500});
   boost::beast::http::read_header(stream, buffer, first_parser, read_error);

   BOOST_REQUIRE_MESSAGE(!read_error, read_error.message());
   const auto& first = first_parser.get();
   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::accepted));
   BOOST_TEST(first.keep_alive());
   BOOST_TEST(first["Content-Length"] == "0");
   BOOST_TEST(first["Content-Type"].empty());

   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /controls/abc/bytes HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n"
      "\r\n"}));

   auto beast_second = beast_http::response<beast_http::string_body>{};
   stream.expires_after(std::chrono::seconds{2});
   beast_http::read(stream, buffer, beast_second);
   auto second = to_http_response(beast_second);
   BOOST_TEST(second.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second.body() == "bytes:abc");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_api_preserves_explicit_method_name_for_same_dto_methods) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<alias_api>(alias_api::describe(), std::make_shared<alias_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<alias_api>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/aliases/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);
   const auto decoded = forge::json::read<control_response>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.value == "legacy:abc");
}

BOOST_AUTO_TEST_CASE(http_api_special_types_support_streaming_put_and_file_get) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto files = temp_directory{};
   auto apis = forge::api::registry{};
   apis.install<object_api>(object_api::describe(), std::make_shared<object_api_impl>(files.path()));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<object_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto object = forge::asio::blocking::run(runtime, forge::http::api::remote<object_api>(client));

   auto payload = std::string(96 * 1024, 'x');
   auto put_body = forge::asio::blocking::run(
      runtime,
      object->put_object(object_put_request{
         .collection = "cache",
         .key = "chunk.bin",
         .content_type = forge::http::header<std::string>{.value = "application/octet-stream", .present = true},
         .digest = forge::http::header<std::string>{.value = "checksum", .present = true},
         .body = forge::http::body_stream{make_body_reader({payload.substr(0, 2048), payload.substr(2048)})},
      }));
   BOOST_TEST(put_body.bytes == 96U * 1024U);
   BOOST_TEST(put_body.content_type == "application/octet-stream");
   BOOST_TEST(put_body.content_md5 == "checksum");

   auto file = forge::asio::blocking::run(
      runtime,
      object->get_object(object_get_request{.collection = "cache", .key = "chunk.bin"}));
   BOOST_TEST(file.status_code() == status::ok);
   BOOST_TEST(file.content_type() == "application/octet-stream");
   auto saved = files.path() / "saved.bin";
   forge::asio::blocking::run(runtime, file.save_to(saved));
   BOOST_TEST(std::filesystem::file_size(saved) == 96U * 1024U);

   auto streamed = forge::asio::blocking::run(
      runtime,
      object->stream_object(object_get_request{.collection = "cache", .key = "chunk.bin"}));
   auto streamed_text = forge::asio::blocking::run(runtime, streamed.body().async_read_all());
   BOOST_TEST(streamed.status_code() == status::ok);
   BOOST_TEST(streamed_text == payload);

   auto head_request = make_request(method::head, "/objects/cache/chunk.bin");
   const auto head_response =
      forge::asio::blocking::run(runtime, connection.async_request(std::move(head_request)));
   BOOST_TEST(head_response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(head_response.body().empty());
   BOOST_TEST(head_response[field::content_length] == std::to_string(96U * 1024U));
   BOOST_TEST(head_response[field::accept_ranges] == "bytes");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_typed_streaming_client_rejects_oversized_error_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto emitted = std::make_shared<std::atomic<unsigned>>(0);

   auto router = forge::http::router{};
   router.get_stream("/objects/:collection/:key/stream",
                     [emitted](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      auto chunks = std::make_shared<std::vector<std::string>>(
         std::vector<std::string>{std::string(40 * 1024, 'a'), std::string(40 * 1024, 'b')});
      auto index = std::make_shared<std::size_t>(0);
      auto reply = response{status::bad_request, request_value.context.request.version()};
      reply.set(field::content_type, "application/json");
      co_return stream_response{
         .head = std::move(reply),
         .body =
            [chunks, index, emitted]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
               if (*index == chunks->size()) {
                  co_return std::nullopt;
               }
               emitted->fetch_add(1);
               co_return make_body_chunk((*chunks)[(*index)++]);
            },
      };
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto object = forge::asio::blocking::run(runtime, forge::http::api::remote<object_api>(client));

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         object->stream_object(object_get_request{.collection = "cache", .key = "chunk.bin"})),
      forge::http::exceptions::payload_too_large);
   BOOST_TEST(emitted->load() == 2U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_file_response_save_to_reports_write_failures) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto files = temp_directory{};

   auto directory_target = files.path() / "directory-target";
   std::filesystem::create_directories(directory_target);
   auto failing = forge::http::file_response::from_body(response{status::ok, 11}, make_body_reader({"payload"}));
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, failing.save_to(directory_target)),
                     forge::http::exceptions::internal);

   auto saved = files.path() / "saved.txt";
   auto valid = forge::http::file_response::from_body(response{status::ok, 11}, make_body_reader({"alpha", "omega"}));
   forge::asio::blocking::run(runtime, valid.save_to(saved));
   auto input = std::ifstream{saved, std::ios::binary};
   auto bytes = std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
   BOOST_TEST(bytes == "alphaomega");
}

BOOST_AUTO_TEST_CASE(http_streaming_response_status_is_owned_by_route_mapping) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto files = temp_directory{};
   std::filesystem::create_directories(files.path() / "cache");
   auto output = std::ofstream{files.path() / "cache" / "chunk.bin", std::ios::binary};
   output << "alpha";
   output.close();

   auto apis = forge::api::registry{};
   apis.install<object_api>(object_api::describe(), std::make_shared<object_api_impl>(files.path()));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding()
                     .use(forge::api::binding().serve(apis).build())
                     .get<&object_api::stream_object, object_get_request, forge::http::streaming_response>(
                        "/objects/:collection/:key/status-stream",
                        forge::http::api::route_options{.response_stream = true,
                                                       .success_status = status::accepted})
                     .build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/objects/cache/chunk.bin/status-stream"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::accepted));
   BOOST_TEST(response.body() == "alpha");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_streamed_response_route_decodes_json_request_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<json_stream_api>(json_stream_api::describe(), std::make_shared<json_stream_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<json_stream_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::post, "/json-stream/item-1");
   request_value.set(field::content_type, "application/json");
   request_value.body() = R"({"id":"item-1","value":"payload"})";
   request_value.prepare_payload();

   const auto response = forge::asio::blocking::run(runtime, client.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response[field::content_type] == "text/plain");
   BOOST_TEST(response.body() == "item-1:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_streamed_response_route_rejects_json_body_path_disagreement) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<json_stream_api>(json_stream_api::describe(), std::make_shared<json_stream_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<json_stream_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::post, "/json-stream/item-1");
   request_value.set(field::content_type, "application/json");
   request_value.body() = R"({"id":"other","value":"payload"})";
   request_value.prepare_payload();

   const auto response = forge::asio::blocking::run(runtime, client.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == 422U);
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find("disagrees") != std::string::npos);

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_streaming_response_from_client_body_is_reserved_when_re_served) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto files = temp_directory{};
   const auto payload = std::string{"alpha-beta-gamma-delta"};
   std::filesystem::create_directories(files.path() / "cache");
   auto output = std::ofstream{files.path() / "cache" / "chunk.bin", std::ios::binary};
   output << payload;
   output.close();

   auto upstream_apis = forge::api::registry{};
   upstream_apis.install<object_api>(object_api::describe(), std::make_shared<object_api_impl>(files.path()));
   auto upstream_router = forge::http::router{};
   upstream_router.mount(forge::http::api::binding()
                            .use(forge::api::binding().serve(upstream_apis).build())
                            .bind<object_api>()
                            .build());
   auto upstream_server = forge::http::server{runtime, server_config{}, std::move(upstream_router)};
   upstream_server.start();

   auto upstream_client =
      forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(upstream_server.port()))};
   auto upstream = forge::asio::blocking::run(runtime, forge::http::api::remote<object_api>(upstream_client));

   auto proxy_apis = forge::api::registry{};
   proxy_apis.install<object_api>(object_api::describe(), std::make_shared<object_proxy_api_impl>(std::move(upstream)));
   auto proxy_router = forge::http::router{};
   proxy_router.mount(forge::http::api::binding().use(forge::api::binding().serve(proxy_apis).build()).bind<object_api>().build());
   auto proxy_server = forge::http::server{runtime, server_config{}, std::move(proxy_router)};
   proxy_server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(proxy_server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/objects/cache/chunk.bin/stream"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response[field::content_type] == "application/octet-stream");
   BOOST_TEST(response.body() == payload);

   proxy_server.stop();
   upstream_server.stop();
}

BOOST_AUTO_TEST_CASE(http_file_response_from_client_body_is_reserved_when_re_served) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto files = temp_directory{};
   const auto payload = std::string(128 * 1024, 'f');
   std::filesystem::create_directories(files.path() / "cache");
   auto output = std::ofstream{files.path() / "cache" / "chunk.bin", std::ios::binary};
   output << payload;
   output.close();

   auto upstream_apis = forge::api::registry{};
   upstream_apis.install<object_api>(object_api::describe(), std::make_shared<object_api_impl>(files.path()));
   auto upstream_router = forge::http::router{};
   upstream_router.mount(forge::http::api::binding()
                            .use(forge::api::binding().serve(upstream_apis).build())
                            .bind<object_api>()
                            .build());
   auto upstream_server = forge::http::server{runtime, server_config{}, std::move(upstream_router)};
   upstream_server.start();

   auto upstream_client =
      forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(upstream_server.port()))};
   auto upstream = forge::asio::blocking::run(runtime, forge::http::api::remote<object_api>(upstream_client));

   auto proxy_apis = forge::api::registry{};
   proxy_apis.install<object_api>(object_api::describe(), std::make_shared<object_proxy_api_impl>(std::move(upstream)));
   auto proxy_router = forge::http::router{};
   proxy_router.mount(forge::http::api::binding().use(forge::api::binding().serve(proxy_apis).build()).bind<object_api>().build());
   auto proxy_server = forge::http::server{runtime, server_config{}, std::move(proxy_router)};
   proxy_server.start();

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(proxy_server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/objects/cache/chunk.bin"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response[field::content_type] == "application/octet-stream");
   BOOST_TEST(response.body() == payload);

   proxy_server.stop();
   upstream_server.stop();
}

BOOST_AUTO_TEST_CASE(http_api_response_file_option_is_required_for_file_response_routes) {
   auto apis = forge::api::registry{};
   auto router = forge::http::router{};
   auto binding = forge::http::api::binding()
                     .use(forge::api::binding().serve(apis).build())
                     .get<&test_api::file_only_api::download, object_get_request, forge::http::file_response>(
                        "/objects/:collection/:key")
                     .build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_api_response_file_option_rejects_non_file_responses) {
   auto apis = forge::api::registry{};
   auto router = forge::http::router{};
   auto binding = forge::http::api::binding()
                     .use(forge::api::binding().serve(apis).build())
                     .get<&api_cache::read, api_read_chunk, api_chunk>(
                        "/cache/chunks/:ref",
                        forge::http::api::route_options{.response_file = true})
                     .build();

   BOOST_CHECK_THROW(router.mount(binding), forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_api_stream_route_does_not_shadow_regular_route_with_same_path) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto files = temp_directory{};
   std::filesystem::create_directories(files.path() / "cache");
   files.write("cache/chunk.bin", "payload");

   auto apis = forge::api::registry{};
   apis.install<object_api>(object_api::describe(), std::make_shared<object_api_impl>(files.path()));

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<object_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto delete_request = make_request(method::delete_, "/objects/cache/chunk.bin");
   const auto delete_response =
      forge::asio::blocking::run(runtime, connection.async_request(std::move(delete_request)));

   BOOST_TEST(delete_response.result_int() == static_cast<unsigned>(status::no_content));
   BOOST_TEST(!std::filesystem::exists(files.path() / "cache" / "chunk.bin"));

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_api_form_only_request_uses_multipart_binding) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::registry{};
   apis.install<form_api>(form_api::describe(), std::make_shared<form_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<form_api>().build();
   router.mount(binding);

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto request_value = make_request(method::post, "/forms");
   request_value.set(field::content_type, "multipart/form-data; boundary=demo");
   request_value.body() =
      "--demo\r\n"
      "Content-Disposition: form-data; name=\"label\"\r\n\r\n"
      "alpha\r\n"
      "--demo\r\n"
      "Content-Disposition: form-data; name=\"count\"\r\n\r\n"
      "7\r\n"
      "--demo--\r\n";
   request_value.prepare_payload();

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));
   const auto decoded = forge::json::read<form_submit_response>(response.body());
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.summary == "alpha:7");

   server.stop();
}

BOOST_AUTO_TEST_CASE(http_api_non_stream_head_route_is_head_only) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<control_api>(control_api::describe(), std::make_shared<control_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<control_api>().build();
   router.mount(binding);

   auto head_request = make_request(method::head, "/controls/abc");
   auto head_context = make_route_context(head_request);
   head_context.runtime = &runtime;
   BOOST_TEST(handle(router, head_context).result_int() == static_cast<unsigned>(status::no_content));

   auto get_request = make_request(method::get, "/controls/abc");
   auto get_context = make_route_context(get_request);
   get_context.runtime = &runtime;
   BOOST_TEST(handle(router, get_context).result_int() == static_cast<unsigned>(status::method_not_allowed));
}

BOOST_AUTO_TEST_CASE(http_api_patch_route_is_mounted) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<patch_api>(patch_api::describe(), std::make_shared<patch_api_impl>());

   auto router = forge::http::router{};
   auto binding = forge::http::api::binding().use(forge::api::binding().serve(apis).build()).bind<patch_api>().build();
   router.mount(binding);

   auto request = make_request(method::patch, "/controls/abc");
   request.set(field::content_type, "application/json");
   request.body() = R"({"id":"abc","value":"updated"})";
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);
   const auto decoded = forge::json::read<control_response>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.value == "abc:updated");
}

BOOST_AUTO_TEST_CASE(middleware_runs_in_order_and_can_short_circuit) {
   auto router = forge::http::router{};
   auto trace = std::make_shared<std::string>();
   router.use([trace](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      *trace += "a>";
      auto response = co_await next();
      *trace += "<a";
      co_return response;
   });
   router.use([trace](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(context);
      *trace += "b>";
      auto response = co_await next();
      *trace += "<b";
      co_return response;
   });
   router.get("/ok", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "ok");
   });

   auto ok_request = make_request(method::get, "/ok");
   auto ok_context = make_route_context(ok_request);
   BOOST_TEST(handle(router, ok_context).body() == "ok");
   BOOST_TEST(*trace == "a>b><b<a");

   auto short_router = forge::http::router{};
   short_router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(next);
      co_return make_text_response(context.request, status::unauthorized, "nope");
   });
   short_router.get("/secure", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "unreachable");
   });
   auto secure_request = make_request(method::get, "/secure");
   auto secure_context = make_route_context(secure_request);
   BOOST_TEST(handle(short_router, secure_context).result_int() == static_cast<unsigned>(status::unauthorized));
}

BOOST_AUTO_TEST_CASE(http_api_plan_mounts_ordered_middleware_contributions) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<throwing_api_cache>());

   auto trace = std::make_shared<std::string>();
   auto plan = forge::api::binding().serve(apis).build();
   auto binding = forge::http::api::binding()
                      .use(std::move(plan))
                      .middleware(forge::http::middleware_descriptor{
                          .id = "limits",
                          .phase = forge::http::middleware_phase::limits,
                          .order = 10,
                          .path_prefix = "/cache",
                          .handler =
                              [trace](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
                                 static_cast<void>(context);
                                 *trace += "limits>";
                                 auto response = co_await next();
                                 *trace += "<limits";
                                 co_return response;
                              },
                      })
                      .middleware(forge::http::middleware_descriptor{
                          .id = "auth",
                          .phase = forge::http::middleware_phase::security,
                          .order = 100,
                          .path_prefix = "/cache",
                          .handler =
                              [trace](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
                                 static_cast<void>(context);
                                 *trace += "auth>";
                                 auto response = co_await next();
                                 *trace += "<auth";
                                 co_return response;
                              },
                      })
                      .get<&api_cache::read, api_read_chunk, api_chunk>("/cache/chunks/:ref")
                      .build();

   auto router = forge::http::router{};
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(*trace == "auth>limits><limits<auth");
}

BOOST_AUTO_TEST_CASE(http_api_plan_mounts_under_base_path) {
   auto runtime = forge::asio::runtime{};
   auto apis = forge::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto trace = std::make_shared<std::string>();
   auto binding = forge::http::api::binding()
                      .use(forge::api::binding().serve(apis).build())
                      .middleware(forge::http::middleware_descriptor{
                          .id = "api-limit",
                          .path_prefix = "/cache",
                          .handler =
                              [trace](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
                                 static_cast<void>(context);
                                 *trace += "limit>";
                                 auto response = co_await next();
                                 *trace += "<limit";
                                 co_return response;
                              },
                      })
                      .bind<macro_cache>()
                      .build();

   auto router = forge::http::router{};
   binding.mount(router, "/api/v1");

   auto request = make_request(method::get, "/api/v1/cache/chunks/abc?offset=7&limit=4096");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto unpacked = forge::json::read<macro_chunk>(response.body());

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_REQUIRE(unpacked.ok());
   BOOST_TEST(unpacked.value.bytes == "abc:7:4096");
   BOOST_TEST(*trace == "limit><limit");

   auto unprefixed_request = make_request(method::get, "/cache/chunks/abc?offset=7&limit=4096");
   auto unprefixed_context = make_route_context(unprefixed_request);
   unprefixed_context.runtime = &runtime;
   BOOST_TEST(handle(router, unprefixed_context).result_int() == static_cast<unsigned>(status::not_found));
}

BOOST_AUTO_TEST_CASE(http_api_plan_rejects_duplicate_middleware_ids) {
   auto duplicate = forge::http::api::binding()
                        .middleware(forge::http::middleware_descriptor{
                            .id = "auth",
                            .handler = [](route_context& context,
                                          next_handler next) -> boost::asio::awaitable<response> {
                               static_cast<void>(context);
                               co_return co_await next();
                            },
                        })
                        .middleware(forge::http::middleware_descriptor{
                            .id = "auth",
                            .handler = [](route_context& context,
                                          next_handler next) -> boost::asio::awaitable<response> {
                               static_cast<void>(context);
                               co_return co_await next();
                            },
                        })
                        .build();

   auto router = forge::http::router{};
   BOOST_CHECK_THROW(router.mount(duplicate), forge::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(middleware_exceptions_return_500) {
   auto router = forge::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(context);
      static_cast<void>(next);
      throw std::runtime_error("boom");
   });
   router.get("/boom", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "unreachable");
   });

   auto request = make_request(method::get, "/boom");
   auto context = make_route_context(request);
   BOOST_TEST(handle(router, context).result_int() == static_cast<unsigned>(status::internal_server_error));
}

BOOST_AUTO_TEST_CASE(client_roundtrips_over_local_server) {
   auto runtime = forge::asio::runtime{};
   auto seen_target = std::make_shared<std::string>();
   auto seen_body = std::make_shared<std::string>();

   auto server = forge::http::server{
       runtime,
       server_config{},
       [seen_target, seen_body](route_context& context) -> boost::asio::awaitable<response> {
          *seen_target = std::string{context.request.target()};
          *seen_body = context.request.body();
          co_return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port) + "/api")};

   const auto response = forge::asio::blocking::run(runtime, client.async_post_json("/v1/info", R"({"ping":1})"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == R"({"ok":true})");
   BOOST_TEST(*seen_target == "/api/v1/info");
   BOOST_TEST(*seen_body == R"({"ping":1})");
   BOOST_CHECK_EQUAL(client.metrics().completed_requests, 1U);
   BOOST_CHECK_EQUAL(client.metrics().status_2xx, 1U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(server_async_start_binds_before_return) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = forge::http::server{
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "ready");
      },
   };

   forge::asio::blocking::run(runtime, server.async_start());

   BOOST_TEST(server.port() != 0U);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_post_json("/health", "{}"));
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "ready");

   forge::asio::blocking::run(runtime, server.async_stop());
   BOOST_TEST(server.port() == 0U);
}

BOOST_AUTO_TEST_CASE(server_rejects_request_body_over_configured_limit) {
   auto runtime = forge::asio::runtime{};
   auto invoked = std::make_shared<std::atomic<bool>>(false);
   auto server = forge::http::server{
      runtime,
      server_config{.max_request_body_bytes = 4},
      [invoked](route_context& context) -> boost::asio::awaitable<response> {
         *invoked = true;
         co_return make_text_response(context.request, status::ok, "unreachable");
      },
   };
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request = make_request(method::post, "/upload");
   request.body() = "12345";
   request.prepare_payload();

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request)));
   BOOST_TEST(response.result_int() == 413);
   BOOST_TEST(!invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(server_keep_alive_gap_uses_idle_timeout) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto requests = std::make_shared<std::atomic<unsigned>>(0);
   auto server = forge::http::server{
      runtime,
      server_config{.read_timeout = std::chrono::seconds{2}, .idle_timeout = std::chrono::milliseconds{50}},
      [requests](route_context& context) -> boost::asio::awaitable<response> {
         requests->fetch_add(1);
         co_return make_text_response(context.request, status::ok, "ok");
      },
   };
   forge::asio::blocking::run(runtime, server.async_start());

   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), server.port()});

   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /first HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
   "\r\n"}));
   auto buffer = beast::flat_buffer{};
   auto beast_first = beast_http::response<beast_http::string_body>{};
   beast_http::read(stream, buffer, beast_first);
   auto first = to_http_response(beast_first);
   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(first.keep_alive());

   std::this_thread::sleep_for(std::chrono::milliseconds{150});
   auto write_error = boost::system::error_code{};
   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /second HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
      "\r\n"}), write_error);

   auto read_error = boost::system::error_code{};
   auto second = beast_http::response<beast_http::string_body>{};
   stream.expires_after(std::chrono::milliseconds{500});
   beast_http::read(stream, buffer, second, read_error);

   BOOST_TEST(read_error != boost::system::error_code{});
   BOOST_TEST(requests->load() == 1U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(server_async_stop_cancels_active_keep_alive_sessions) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto requests = std::make_shared<std::atomic<unsigned>>(0);
   auto server = forge::http::server{
      runtime,
      server_config{.read_timeout = std::chrono::seconds{5}, .idle_timeout = std::chrono::seconds{5}},
      [requests](route_context& context) -> boost::asio::awaitable<response> {
         requests->fetch_add(1);
         co_return make_text_response(context.request, status::ok, "ok");
      },
   };
   forge::asio::blocking::run(runtime, server.async_start());

   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), server.port()});

   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /first HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
   "\r\n"}));
   auto buffer = beast::flat_buffer{};
   auto beast_first = beast_http::response<beast_http::string_body>{};
   beast_http::read(stream, buffer, beast_first);
   auto first = to_http_response(beast_first);
   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(first.keep_alive());

   forge::asio::blocking::run(runtime, server.async_stop());

   auto write_error = boost::system::error_code{};
   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /second HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "\r\n"}), write_error);

   auto read_error = boost::system::error_code{};
   auto second = beast_http::response<beast_http::string_body>{};
   stream.expires_after(std::chrono::milliseconds{500});
   beast_http::read(stream, buffer, second, read_error);

   BOOST_TEST(read_error != boost::system::error_code{});
   BOOST_TEST(requests->load() == 1U);
}

BOOST_AUTO_TEST_CASE(server_stop_waits_for_executor_work_before_returning) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto mutex = std::mutex{};
   auto ready = std::condition_variable{};
   auto release = std::condition_variable{};
   auto handler_started = false;
   auto handler_released = false;

   auto server = forge::http::server{
      runtime,
      server_config{.read_timeout = std::chrono::seconds{5}, .idle_timeout = std::chrono::seconds{5}},
      [&](route_context& context) -> boost::asio::awaitable<response> {
         {
            auto lock = std::unique_lock{mutex};
            handler_started = true;
            ready.notify_all();
            release.wait(lock, [&] { return handler_released; });
         }
         co_return make_text_response(context.request, status::ok, "released");
      },
   };
   forge::asio::blocking::run(runtime, server.async_start());

   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), server.port()});
   asio::write(stream.socket(), asio::buffer(std::string{
      "GET /blocked HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
      "\r\n"}));

   {
      auto lock = std::unique_lock{mutex};
      BOOST_REQUIRE(ready.wait_for(lock, std::chrono::seconds{2}, [&] { return handler_started; }));
   }

   auto stop_returned = std::atomic_bool{false};
   auto stop_thread = std::thread{[&] {
      server.stop();
      stop_returned.store(true);
   }};

   std::this_thread::sleep_for(std::chrono::milliseconds{100});
   BOOST_TEST(!stop_returned.load());

   {
      const auto lock = std::scoped_lock{mutex};
      handler_released = true;
   }
   release.notify_all();
   stop_thread.join();
   BOOST_TEST(stop_returned.load());

   auto buffer = beast::flat_buffer{};
   auto response_value = beast_http::response<beast_http::string_body>{};
   auto read_error = boost::system::error_code{};
   stream.expires_after(std::chrono::milliseconds{500});
   beast_http::read(stream, buffer, response_value, read_error);
   if (!read_error) {
      asio::write(stream.socket(), asio::buffer(std::string{
         "GET /after-stop HTTP/1.1\r\n"
         "Host: 127.0.0.1\r\n"
         "\r\n"}), read_error);
      auto after_stop = beast_http::response<beast_http::string_body>{};
      stream.expires_after(std::chrono::milliseconds{500});
      beast_http::read(stream, buffer, after_stop, read_error);
   }
   BOOST_TEST(read_error != boost::system::error_code{});
}

BOOST_AUTO_TEST_CASE(server_stop_without_start_returns_without_executor_state_race) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto server = forge::http::server{
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "unused");
      },
   };

   server.stop();
   server.stop();
}

BOOST_AUTO_TEST_CASE(server_stop_called_from_runtime_worker_does_not_deadlock) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = std::make_shared<forge::http::server>(
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "unused");
      });
   forge::asio::blocking::run(runtime, server->async_start());

   auto stopped = std::make_shared<std::promise<void>>();
   auto stopped_future = stopped->get_future();
   asio::post(runtime.context(), [server, stopped] {
      server->stop();
      stopped->set_value();
   });

   BOOST_REQUIRE(stopped_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   forge::asio::blocking::run(runtime, server->async_stop());
}

BOOST_AUTO_TEST_CASE(server_accept_loop_survives_runtime_worker_stop_and_server_destruction) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = std::make_shared<std::optional<forge::http::server>>();
   server->emplace(
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "unused");
      });
   forge::asio::blocking::run(runtime, (*server)->async_start());

   auto stopped = std::make_shared<std::promise<void>>();
   auto stopped_future = stopped->get_future();
   asio::post(runtime.context(), [server, stopped] {
      (*server)->stop();
      server->reset();
      stopped->set_value();
   });

   BOOST_REQUIRE(stopped_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);

   auto drained = std::make_shared<std::promise<void>>();
   auto drained_future = drained->get_future();
   asio::post(runtime.context(), [drained] {
      drained->set_value();
   });
   BOOST_REQUIRE(drained_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
}

BOOST_AUTO_TEST_CASE(server_stop_after_runtime_stop_returns_without_waiting) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto server = std::make_unique<forge::http::server>(
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "unused");
      });
   forge::asio::blocking::run(runtime, server->async_start());

   runtime.stop();

   const auto started = std::chrono::steady_clock::now();
   server->stop();
   const auto elapsed = std::chrono::steady_clock::now() - started;

   BOOST_TEST(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 500);
   server.reset();
}

BOOST_AUTO_TEST_CASE(server_start_waits_for_executor_work_before_returning) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto mutex = std::mutex{};
   auto ready = std::condition_variable{};
   auto release = std::condition_variable{};
   auto blocker_started = false;
   auto blocker_released = false;

   asio::post(runtime.context(), [&] {
      auto lock = std::unique_lock{mutex};
      blocker_started = true;
      ready.notify_all();
      release.wait(lock, [&] { return blocker_released; });
   });

   {
      auto lock = std::unique_lock{mutex};
      BOOST_REQUIRE(ready.wait_for(lock, std::chrono::seconds{2}, [&] { return blocker_started; }));
   }

   auto server = forge::http::server{
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "started");
      },
   };

   auto start_returned = std::atomic_bool{false};
   auto start_thread = std::thread{[&] {
      server.start();
      start_returned.store(true);
   }};

   std::this_thread::sleep_for(std::chrono::milliseconds{100});
   BOOST_TEST(!start_returned.load());

   {
      const auto lock = std::scoped_lock{mutex};
      blocker_released = true;
   }
   release.notify_all();
   start_thread.join();
   BOOST_TEST(start_returned.load());

   const auto port = wait_for_port(server);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/started"));
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "started");

   server.stop();
}

BOOST_AUTO_TEST_CASE(server_start_called_from_runtime_worker_fails_without_deadlock) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto server = std::make_shared<forge::http::server>(
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "unused");
      });

   auto result = std::make_shared<std::promise<std::exception_ptr>>();
   auto future = result->get_future();
   asio::post(runtime.context(), [server, result] {
      try {
         server->start();
         result->set_value(nullptr);
      } catch (...) {
         result->set_value(std::current_exception());
      }
   });

   BOOST_REQUIRE(future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   auto error = future.get();
   BOOST_REQUIRE(error != nullptr);

   try {
      std::rethrow_exception(error);
   } catch (const forge::http::exceptions::internal& value) {
      BOOST_TEST(std::string{value.what()}.find("async_start") != std::string::npos);
   } catch (...) {
      BOOST_FAIL("server::start() threw an unexpected exception type");
   }
}

BOOST_AUTO_TEST_CASE(http_stream_route_reads_large_request_body_in_chunks) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto total_bytes = std::make_shared<std::atomic<std::size_t>>(0);
   auto chunk_count = std::make_shared<std::atomic<std::size_t>>(0);
   auto header_body_empty = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.post_stream("/upload",
                      [total_bytes, chunk_count, header_body_empty](stream_request& request_value)
                         -> boost::asio::awaitable<stream_response> {
                         header_body_empty->store(request_value.context.request.body().empty());
                         while (auto chunk = co_await request_value.body.async_read()) {
                            total_bytes->fetch_add(chunk->bytes.size());
                            chunk_count->fetch_add(1);
                         }
                         auto reply = make_text_response(request_value.context.request, status::ok,
                                                         std::to_string(total_bytes->load()));
                         co_return stream_response::buffered(std::move(reply));
                      });

   auto server = forge::http::server{runtime, server_config{.max_request_body_bytes = 512 * 1024}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::post, "/upload");
   request_value.body().assign(256 * 1024, 'x');
   request_value.prepare_payload();

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == std::to_string(256 * 1024));
   BOOST_TEST(total_bytes->load() == 256U * 1024U);
   BOOST_TEST(chunk_count->load() > 1U);
   BOOST_TEST(header_body_empty->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_route_writes_chunked_response_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto produced = std::make_shared<std::atomic<std::size_t>>(0);

   auto router = forge::http::router{};
   router.get_stream("/download", [produced](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      auto chunks = std::make_shared<std::vector<std::string>>(
         std::vector<std::string>{"alpha", "-", "omega"});
      auto index = std::make_shared<std::size_t>(0);
      auto reply = response{status::ok, request_value.context.request.version()};
      reply.set(field::content_type, "application/octet-stream");
      co_return stream_response{
          .head = std::move(reply),
          .body =
             [chunks, index, produced]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
                if (*index == chunks->size()) {
                   co_return std::nullopt;
                }
                produced->fetch_add(1);
                co_return make_body_chunk((*chunks)[(*index)++]);
             },
      };
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/download"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "alpha-omega");
   BOOST_TEST(response[field::transfer_encoding] == "chunked");
   BOOST_TEST(produced->load() == 3U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_route_response_passes_through_after_middleware) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<unsigned>>(0);
   auto produced = std::make_shared<std::atomic<unsigned>>(0);

   auto router = forge::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      auto result = co_await next();
      result.set(field::server, "forge-stream");
      result.set("X-Stream-Middleware", context.parsed_target.path);
      co_return result;
   });
   router.get_stream("/download", [invoked, produced](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      invoked->fetch_add(1);
      auto chunks = std::make_shared<std::vector<std::string>>(std::vector<std::string>{"alpha", "omega"});
      auto index = std::make_shared<std::size_t>(0);
      auto reply = response{status::ok, request_value.context.request.version()};
      reply.set(field::content_type, "application/octet-stream");
      co_return stream_response{
          .head = std::move(reply),
          .body =
             [chunks, index, produced]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
                if (*index == chunks->size()) {
                   co_return std::nullopt;
                }
                produced->fetch_add(1);
                co_return make_body_chunk((*chunks)[(*index)++]);
             },
      };
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/download"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "alphaomega");
   BOOST_TEST(std::string{response[field::server]} == "forge-stream");
   BOOST_TEST(std::string{response["X-Stream-Middleware"]} == "/download");
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(invoked->load() == 1U);
   BOOST_TEST(produced->load() == 2U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_middleware_replacement_does_not_leak_original_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<unsigned>>(0);
   auto produced = std::make_shared<std::atomic<unsigned>>(0);

   auto router = forge::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(co_await next());
      co_return make_text_response(context.request, status::forbidden, "blocked");
   });
   router.get_stream("/download", [invoked, produced](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      invoked->fetch_add(1);
      auto chunks = std::make_shared<std::vector<std::string>>(std::vector<std::string>{"secret", "-stream"});
      auto index = std::make_shared<std::size_t>(0);
      auto reply = response{status::ok, request_value.context.request.version()};
      reply.set(field::content_type, "application/octet-stream");
      co_return stream_response{
          .head = std::move(reply),
          .body =
             [chunks, index, produced]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
                if (*index == chunks->size()) {
                   co_return std::nullopt;
                }
                produced->fetch_add(1);
                co_return make_body_chunk((*chunks)[(*index)++]);
             },
      };
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/download"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::forbidden));
   BOOST_TEST(response.body() == "blocked");
   const auto has_transfer_encoding = response.find(field::transfer_encoding) != response.end();
   BOOST_TEST(!has_transfer_encoding);
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(invoked->load() == 1U);
   BOOST_TEST(produced->load() == 0U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_middleware_short_circuits_before_body_read) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(next);
      co_return make_text_response(context.request, status::unauthorized, "blocked");
   });
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      invoked->store(true);
      co_return stream_response::buffered(response{status::ok, 11});
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto response = raw_http_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 1048576\r\n\r\n");

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::unauthorized));
   BOOST_TEST(response.body() == "blocked");
   BOOST_TEST(!invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_accepted_stream_sends_interim_before_body_read) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto bytes_read = std::make_shared<std::atomic<std::size_t>>(0);

   auto router = forge::http::router{};
   router.post_stream("/upload", [bytes_read](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      const auto body = co_await request_value.body.async_read_all();
      bytes_read->store(body.size());
      co_return stream_response::buffered(make_text_response(request_value.context.request, status::ok, body));
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto body = std::string{"large-payload"};
   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: " +
         std::to_string(body.size()) + "\r\n"
         "\r\n",
      body,
      std::chrono::milliseconds{500});

   BOOST_REQUIRE(exchange.interim.has_value());
   BOOST_TEST(exchange.interim->result_int() == static_cast<unsigned>(status::continue_));
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(exchange.final.body() == body);
   BOOST_TEST(bytes_read->load() == body.size());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_body_backed_stream_sends_interim_before_final_headers) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto router = forge::http::router{};
   router.post_stream("/upload", [](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      auto reply = response{status::ok, request_value.context.request.version()};
      reply.set(field::content_type, "application/octet-stream");
      co_return forge::http::streaming_response::from_body(std::move(reply), std::move(request_value.body))
         .materialize(request_value.context.request, status::ok);
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto body = std::string{"body-backed-payload"};
   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: " +
         std::to_string(body.size()) + "\r\n"
         "\r\n",
      body,
      std::chrono::milliseconds{500});

   BOOST_REQUIRE(exchange.interim.has_value());
   BOOST_TEST(exchange.interim->result_int() == static_cast<unsigned>(status::continue_));
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(exchange.final.body() == body);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_copied_body_backed_stream_sends_interim_before_final_headers) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto router = forge::http::router{};
   router.post_stream("/upload", [](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      auto reply = response{status::ok, request_value.context.request.version()};
      reply.set(field::content_type, "application/octet-stream");
      auto copied_body = request_value.body;
      co_return forge::http::streaming_response::from_body(std::move(reply), std::move(copied_body))
         .materialize(request_value.context.request, status::ok);
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto body = std::string{"copied-body-backed-payload"};
   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: " +
         std::to_string(body.size()) + "\r\n"
         "\r\n",
      body,
      std::chrono::milliseconds{500});

   BOOST_REQUIRE(exchange.interim.has_value());
   BOOST_TEST(exchange.interim->result_int() == static_cast<unsigned>(status::continue_));
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(exchange.final.body() == body);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_custom_body_callback_reading_request_body_sends_interim_before_final_headers) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto router = forge::http::router{};
   router.post_stream("/upload", [](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      auto reply = response{status::ok, request_value.context.request.version()};
      reply.set(field::content_type, "application/octet-stream");
      auto copied_body = request_value.body;
      auto callback_body = copied_body;
      co_return stream_response{
         .head = std::move(reply),
         .body = stream_response::body_source{
            copied_body,
            [body = std::move(callback_body)]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
               co_return co_await body.async_read();
            },
         },
      };
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto body = std::string{"custom-callback-body-backed-payload"};
   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: " +
         std::to_string(body.size()) + "\r\n"
         "\r\n",
      body,
      std::chrono::milliseconds{500});

   BOOST_REQUIRE(exchange.interim.has_value());
   BOOST_TEST(exchange.interim->result_int() == static_cast<unsigned>(status::continue_));
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(exchange.final.body() == body);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_moved_body_buffered_rejection_does_not_send_interim) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      invoked->store(true);
      auto moved_body = std::move(request_value.body);
      static_cast<void>(moved_body);
      co_return stream_response::buffered(make_text_response(request_value.context.request, status::forbidden, "blocked"));
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: 1048576\r\n"
      "\r\n",
      std::string(1024, 'x'),
      std::chrono::milliseconds{500});

   BOOST_TEST(!exchange.interim.has_value());
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::forbidden));
   BOOST_TEST(exchange.final.body() == "blocked");
   BOOST_TEST(invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_streaming_response_from_body_does_not_expose_internal_headers) {
   auto reply = response{status::ok, 11};
   reply.set(field::content_type, "application/octet-stream");

   const auto streamed = forge::http::streaming_response::from_body(std::move(reply), make_body_reader({"payload"}));

   BOOST_TEST(!has_internal_forge_header(streamed.head()));
}

BOOST_AUTO_TEST_CASE(http_expect_continue_public_from_body_stream_without_request_body_rejects_without_interim) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      invoked->store(true);
      auto reply = response{status::forbidden, request_value.context.request.version()};
      reply.set(field::content_type, "text/plain");
      co_return forge::http::streaming_response::from_body(std::move(reply), make_body_reader({"blocked"}))
         .materialize(request_value.context.request, status::forbidden);
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: 1048576\r\n"
      "\r\n",
      std::string(1024, 'x'),
      std::chrono::milliseconds{500});

   BOOST_TEST(!exchange.interim.has_value());
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::forbidden));
   BOOST_TEST(exchange.final.body() == "blocked");
   BOOST_TEST(invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_stream_response_without_request_body_rejects_without_interim) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      invoked->store(true);
      auto reply = response{status::forbidden, 11};
      reply.set(field::content_type, "text/plain");
      co_return stream_response{
         .head = std::move(reply),
         .body =
            [sent = false]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
            if (sent) {
               co_return std::nullopt;
            }
            sent = true;
            co_return make_body_chunk("blocked");
         },
      };
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: 1048576\r\n"
      "\r\n",
      std::string(1024, 'x'),
      std::chrono::milliseconds{500});

   BOOST_TEST(!exchange.interim.has_value());
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::forbidden));
   BOOST_TEST(exchange.final.body() == "blocked");
   BOOST_TEST(invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_missing_path_rejects_without_interim) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto router = forge::http::router{};
   router.post_stream("/upload", [](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      co_return stream_response::buffered(response{status::ok, 11});
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /missing HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: 1048576\r\n"
      "\r\n",
      std::string(1024, 'x'),
      std::chrono::milliseconds{500});

   BOOST_TEST(!exchange.interim.has_value());
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::not_found));

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_wrong_method_on_stream_route_rejects_without_interim) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      invoked->store(true);
      co_return stream_response::buffered(response{status::ok, 11});
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "PUT /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: 1048576\r\n"
      "\r\n",
      std::string(1024, 'x'),
      std::chrono::milliseconds{500});

   BOOST_TEST(!exchange.interim.has_value());
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::method_not_allowed));
   BOOST_TEST(!invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_preflight_bodyless_rejection_preserves_keep_alive) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto router = forge::http::router{};
   router.get("/ok", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "still-open");
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto [first, second] = raw_two_request_exchange(
      server.port(),
      "GET /missing HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: keep-alive\r\n"
      "\r\n",
      "GET /ok HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n"
      "\r\n");

   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(first.keep_alive());
   BOOST_TEST(second.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second.body() == "still-open");

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_accepted_buffered_route_sends_interim_before_body_read) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto bytes_read = std::make_shared<std::atomic<std::size_t>>(0);

   auto router = forge::http::router{};
   router.post("/buffered", [bytes_read](route_context& context) -> boost::asio::awaitable<response> {
      bytes_read->store(context.request.body().size());
      co_return make_text_response(context.request, status::ok, context.request.body());
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto body = std::string{"buffered-payload"};
   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /buffered HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: " +
         std::to_string(body.size()) + "\r\n"
         "\r\n",
      body,
      std::chrono::milliseconds{500});

   BOOST_REQUIRE(exchange.interim.has_value());
   BOOST_TEST(exchange.interim->result_int() == static_cast<unsigned>(status::continue_));
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(exchange.final.body() == body);
   BOOST_TEST(bytes_read->load() == body.size());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_expect_continue_rejection_does_not_consume_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(next);
      co_return make_text_response(context.request, status::unauthorized, "blocked");
   });
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      invoked->store(true);
      co_return stream_response::buffered(response{status::ok, 11});
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto exchange = raw_expect_continue_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Expect: 100-continue\r\n"
      "Content-Length: 1048576\r\n"
      "\r\n",
      std::string(1024, 'x'),
      std::chrono::milliseconds{500});

   BOOST_TEST(!exchange.interim.has_value());
   BOOST_TEST(exchange.final.result_int() == static_cast<unsigned>(status::unauthorized));
   BOOST_TEST(exchange.final.body() == "blocked");
   BOOST_TEST(!invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_short_circuit_closes_connection_with_unread_body) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto upload_invoked = std::make_shared<std::atomic<bool>>(false);
   auto ok_requests = std::make_shared<std::atomic<unsigned>>(0);

   auto router = forge::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(next);
      if (context.parsed_target.path == "/upload") {
         co_return make_text_response(context.request, status::unauthorized, "blocked");
      }
      co_return co_await next();
   });
   router.post_stream("/upload", [upload_invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      upload_invoked->store(true);
      co_return stream_response::buffered(response{status::ok, 11});
   });
   router.get("/ok", [ok_requests](route_context& context) -> boost::asio::awaitable<response> {
      ok_requests->fetch_add(1);
      co_return make_text_response(context.request, status::ok, "ok");
   });

   auto server = forge::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto unread_body = std::string{"GET /ok HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"};
   const auto response = raw_http_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Content-Length: " +
         std::to_string(unread_body.size()) + "\r\n"
         "\r\n" +
         unread_body,
      std::chrono::milliseconds{100});

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::unauthorized));
   BOOST_TEST(response.body() == "blocked");
   BOOST_TEST(!response.keep_alive());
   BOOST_TEST(!upload_invoked->load());
   BOOST_TEST(ok_requests->load() == 0U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_body_limit_fires_during_stream_read) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = forge::http::router{};
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      invoked->store(true);
      while (co_await request_value.body.async_read()) {
      }
      co_return stream_response::buffered(make_text_response(request_value.context.request, status::ok, "ok"));
   });

   auto server = forge::http::server{runtime, server_config{.max_request_body_bytes = 4}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   const auto response = raw_http_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\n"
      "12345\r\n"
      "0\r\n"
      "\r\n");

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::payload_too_large));
   BOOST_TEST(response.body().find("payload_too_large") != std::string::npos);
   BOOST_TEST(invoked->load());

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_serves_full_file_stream) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path(), file_options{.content_type = "application/test"});

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = forge::asio::blocking::run(runtime, client.async_get("/files/chunk.bin"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "0123456789");
   BOOST_TEST(response[field::content_type] == "application/test");
   BOOST_TEST(response[field::accept_ranges] == "bytes");
   BOOST_TEST(response[field::etag].size() > 0U);
   BOOST_TEST(response[field::last_modified].size() > 0U);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_serves_byte_range) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::get, "/files/chunk.bin");
   request_value.set(field::range, "bytes=2-5");

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::partial_content));
   BOOST_TEST(response.body() == "2345");
   BOOST_TEST(response[field::content_range] == "bytes 2-5/10");
   BOOST_TEST(response[field::content_length] == "4");

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_ignores_unsupported_multi_range) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::get, "/files/chunk.bin");
   request_value.set(field::range, "bytes=0-1,4-5");

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "0123456789");
   const auto has_content_range = response.find(field::content_range) != response.end();
   BOOST_TEST(!has_content_range);
   BOOST_TEST(response[field::content_length] == "10");

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_rejects_invalid_range) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::get, "/files/chunk.bin");
   request_value.set(field::range, "bytes=20-30");

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::range_not_satisfiable));
   BOOST_TEST(response.body().empty());
   BOOST_TEST(response[field::content_range] == "bytes */10");

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_uses_deterministic_weak_etag) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto first = forge::asio::blocking::run(runtime, connection.async_request(make_request(method::get, "/files/chunk.bin")));

   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(std::string{first[field::etag]}.starts_with("W/\""));

   auto conditional = make_request(method::get, "/files/chunk.bin");
   conditional.set(field::if_none_match, first[field::etag]);
   const auto second = forge::asio::blocking::run(runtime, connection.async_request(std::move(conditional)));

   BOOST_TEST(second.result_int() == static_cast<unsigned>(status::not_modified));
   BOOST_TEST(second[field::etag] == first[field::etag]);

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_head_returns_headers_without_body) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.head_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());

   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::head, "/files/chunk.bin");

   const auto response = forge::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body().empty());
   BOOST_TEST(response[field::content_length] == "10");
   BOOST_TEST(response[field::accept_ranges] == "bytes");

   forge::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_rejects_traversal_and_symlink) {
   auto files = temp_directory{};
   files.write("visible.bin", "visible");
   auto outside = std::filesystem::temp_directory_path() / "forge-http-outside-secret.bin";
   {
      auto output = std::ofstream{outside, std::ios::binary};
      output << "secret";
   }
   const auto link = files.path() / "link.bin";
   std::error_code symlink_error;
   std::filesystem::create_symlink(outside, link, symlink_error);

   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto root = static_file_root{files.path()};
   auto request_value = make_request(method::get, "/files/link.bin");
   auto context = make_route_context(request_value);
   auto stream_request_value = stream_request{.context = context, .body = body_reader{}};

   const auto traversal = forge::asio::blocking::run(runtime, root.serve(stream_request_value, "../secret.bin"));
   BOOST_TEST(traversal.head.result_int() == static_cast<unsigned>(status::forbidden));

   if (!symlink_error) {
      const auto symlink = forge::asio::blocking::run(runtime, root.serve(stream_request_value, "link.bin"));
      BOOST_TEST(symlink.head.result_int() == static_cast<unsigned>(status::forbidden));
   }

   std::error_code ignored;
   std::filesystem::remove(outside, ignored);
}

BOOST_AUTO_TEST_CASE(http_static_file_root_rejects_backslash_and_symlink_escape_when_following) {
   auto files = temp_directory{};
   files.write("visible.bin", "visible");
   auto outside = std::filesystem::temp_directory_path() / "forge-http-follow-outside-secret.bin";
   {
      auto output = std::ofstream{outside, std::ios::binary};
      output << "secret";
   }
   const auto link = files.path() / "follow.bin";
   std::error_code symlink_error;
   std::filesystem::create_symlink(outside, link, symlink_error);

   auto runtime = forge::asio::runtime{};
   auto root = static_file_root{files.path(), file_options{.symlinks = symlink_policy::follow}};
   auto request_value = make_request(method::get, "/files/follow.bin");
   auto context = make_route_context(request_value);
   auto stream_request_value = stream_request{.context = context, .body = body_reader{}};

   const auto backslash = forge::asio::blocking::run(runtime, root.serve(stream_request_value, "..\\secret.bin"));
   BOOST_TEST(backslash.head.result_int() == static_cast<unsigned>(status::forbidden));

   if (!symlink_error) {
      const auto escaped = forge::asio::blocking::run(runtime, root.serve(stream_request_value, "follow.bin"));
      BOOST_TEST(escaped.head.result_int() == static_cast<unsigned>(status::forbidden));
   }

   std::error_code ignored;
   std::filesystem::remove(outside, ignored);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_keeps_small_upload_in_memory) {
   auto runtime = forge::asio::runtime{};
   auto reader = upload_reader{make_body_reader({"hello", " ", "world"}),
                               upload_options{.memory_threshold_bytes = 64, .max_file_bytes = 1024,
                                              .max_total_bytes = 1024}};

   const auto part = forge::asio::blocking::run(runtime, reader.async_read());

   BOOST_TEST(part.in_memory());
   BOOST_TEST(!part.spool.has_value());
   BOOST_TEST(part.size == 11U);
   BOOST_TEST(part.text() == "hello world");
}

BOOST_AUTO_TEST_CASE(http_upload_reader_spools_large_upload_and_cleans_up) {
   auto files = temp_directory{};
   auto runtime = forge::asio::runtime{};
   auto spool_path = std::filesystem::path{};
   {
      auto reader = upload_reader{make_body_reader({"0123", "4567", "89"}),
                                  upload_options{.memory_threshold_bytes = 4, .max_file_bytes = 1024,
                                                 .max_total_bytes = 1024, .spool_directory = files.path()}};

      const auto part = forge::asio::blocking::run(runtime, reader.async_read());

      BOOST_TEST(!part.in_memory());
      BOOST_TEST(part.spool.has_value());
      BOOST_TEST(part.size == 10U);
      BOOST_TEST(part.text() == "0123456789");
      spool_path = part.spool->path();
      BOOST_TEST(std::filesystem::is_regular_file(spool_path));
   }

   BOOST_TEST(!std::filesystem::exists(spool_path));
}

BOOST_AUTO_TEST_CASE(http_upload_reader_multipart_extracts_fields_and_files) {
   auto files = temp_directory{};
   auto runtime = forge::asio::runtime{};
   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"title\"\r\n"
                  "\r\n"
                  "sample\r\n"
                  "--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n"
                  "abcdefghi\r\n"
                  "--demo--\r\n"};
   auto reader = upload_reader{make_body_reader({body}),
                               upload_options{.memory_threshold_bytes = 4, .max_file_bytes = 1024,
                                              .max_total_bytes = 1024, .spool_directory = files.path()}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));

   BOOST_REQUIRE_EQUAL(form.parts.size(), 2U);
   const auto title = form.field("title");
   BOOST_REQUIRE(title.has_value());
   BOOST_TEST(*title == "sample");
   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   BOOST_TEST(form.files.front().name == "file");
   BOOST_REQUIRE(form.files.front().filename.has_value());
   BOOST_TEST(*form.files.front().filename == "chunk.txt");
   BOOST_TEST(form.files.front().content_type == "text/plain");
   BOOST_TEST(!form.files.front().in_memory());
   BOOST_TEST(form.files.front().text() == "abcdefghi");
}

BOOST_AUTO_TEST_CASE(http_multipart_writer_generates_safe_boundaries_and_quoted_headers) {
   auto runtime = forge::asio::runtime{};
   const auto payload = std::string{"alpha\r\n--forge-http-boundary\r\nomega"};
   auto written = write_multipart_form({
      multipart_writer_part{.name = "title", .body = payload},
      multipart_writer_part{
         .name = "file",
         .filename = std::string{"..\\secret\"quoted.txt"},
         .content_type = "text/plain",
         .body = "file-body",
      },
   });

   BOOST_TEST(written.content_type.find("forge-http-boundary") == std::string::npos);
   BOOST_TEST(written.body.find("\r\nX-Injected:") == std::string::npos);

   const auto boundary = multipart_boundary(written.content_type);
   BOOST_REQUIRE(boundary.has_value());
   auto reader = upload_reader{make_body_reader({written.body}), upload_options{.max_total_bytes = 4096}};
   const auto form = forge::asio::blocking::run(runtime, reader.async_read_multipart(written.content_type));

   const auto title = form.field("title");
   BOOST_REQUIRE(title.has_value());
   BOOST_TEST(*title == payload);
   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   BOOST_TEST(form.files.front().name == "file");
   BOOST_TEST(form.files.front().content_type == "text/plain");
   BOOST_TEST(form.files.front().text() == "file-body");
}

BOOST_AUTO_TEST_CASE(http_multipart_writer_rejects_control_bytes_in_parameters) {
   BOOST_CHECK_THROW(
      static_cast<void>(write_multipart_form({
         multipart_writer_part{
            .name = std::string{"field\r\nX-Injected: yes"},
            .body = "value",
         },
      })),
      forge::http::exceptions::bad_request);

   BOOST_CHECK_THROW(
      static_cast<void>(write_multipart_form({
         multipart_writer_part{
            .name = std::string{"field\033name"},
            .body = "value",
         },
      })),
      forge::http::exceptions::bad_request);

   BOOST_CHECK_THROW(
      static_cast<void>(write_multipart_form({
         multipart_writer_part{
            .name = "file",
            .filename = std::string{"bad\0name.txt", 12},
            .body = "file-body",
         },
      })),
      forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_multipart_writer_rejects_crlf_in_content_type) {
   BOOST_CHECK_THROW(
      static_cast<void>(write_multipart_form({
         multipart_writer_part{
            .name = "file",
            .filename = std::string{"chunk.txt"},
            .content_type = "text/plain\r\nX-Injected: yes",
            .body = "file-body",
         },
      })),
      forge::http::exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_rejects_malformed_multipart_boundary) {
   auto runtime = forge::asio::runtime{};
   auto reader = upload_reader{make_body_reader({"--demo\r\nContent-Disposition: form-data; name=\"x\"\r\n\r\nvalue\r\n"}),
                               upload_options{.memory_threshold_bytes = 64, .max_file_bytes = 1024,
                                              .max_total_bytes = 1024}};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, reader.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_accepts_boundary_like_bytes_inside_file_content) {
   auto runtime = forge::asio::runtime{};
   const auto content = std::string{"alpha\r\n--demo-not-a-delimiter\r\nomega"};
   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n"} +
      content +
      "\r\n--demo--\r\n";
   auto reader = upload_reader{make_body_reader({body}),
                               upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 1024,
                                              .max_total_bytes = 1024}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));

   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   BOOST_TEST(form.files.front().text() == content);

   auto malformed = upload_reader{
      make_body_reader({"--demo\r\n"
                        "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                        "\r\n"
                        "alpha\r\n"
                        "--demo-not-a-delimiter\r\n"
                        "omega\r\n"}),
      upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 1024, .max_total_bytes = 1024}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, malformed.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::bad_request);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_accepts_closing_boundary_like_bytes_inside_file_content) {
   auto runtime = forge::asio::runtime{};
   const auto content = std::string{"alpha\r\n--demo--not-a-delimiter\r\nomega"};
   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n"} +
      content +
      "\r\n--demo--\r\n";
   auto reader = upload_reader{make_body_reader({body}),
                               upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 1024,
                                              .max_total_bytes = 1024}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));

   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   BOOST_TEST(form.files.front().text() == content);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_enforces_file_and_total_limits) {
   auto runtime = forge::asio::runtime{};
   auto total_limited = upload_reader{make_body_reader({"123", "45"}),
                                      upload_options{.memory_threshold_bytes = 64, .max_file_bytes = 1024,
                                                     .max_total_bytes = 4}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, total_limited.async_read()), exceptions::payload_too_large);

   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                  "\r\n"
                  "12345\r\n"
                  "--demo--\r\n"};
   auto file_limited = upload_reader{make_body_reader({body}),
                                     upload_options{.memory_threshold_bytes = 64, .max_file_bytes = 4,
                                                    .max_total_bytes = 1024}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, file_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_multipart_limits_non_file_fields) {
   auto runtime = forge::asio::runtime{};
   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"notes\"\r\n"
                  "\r\n"
                  "123456\r\n"
                  "--demo--\r\n"};
   auto reader = upload_reader{make_body_reader({body}),
                               upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 1024,
                                              .max_field_bytes = 5, .max_total_bytes = 1024}};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, reader.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_multipart_accepts_configured_count_limits) {
   auto runtime = forge::asio::runtime{};
   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"first\"\r\n"
                  "\r\n"
                  "1\r\n"
                  "--demo\r\n"
                  "Content-Disposition: form-data; name=\"second\"\r\n"
                  "\r\n"
                  "2\r\n"
                  "--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                  "\r\n"
                  "abc\r\n"
                  "--demo--\r\n"};
   auto reader = upload_reader{make_body_reader({body}),
                               upload_options{.memory_threshold_bytes = 256,
                                              .max_file_bytes = 8,
                                              .max_field_bytes = 8,
                                              .max_total_bytes = 1024,
                                              .max_parts = 3,
                                              .max_files = 1,
                                              .max_fields = 2}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));

   BOOST_REQUIRE_EQUAL(form.parts.size(), 3U);
   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   const auto first = form.field("first");
   const auto second = form.field("second");
   BOOST_REQUIRE(first.has_value());
   BOOST_REQUIRE(second.has_value());
   BOOST_TEST(*first == "1");
   BOOST_TEST(*second == "2");
}

BOOST_AUTO_TEST_CASE(http_upload_reader_multipart_enforces_part_file_field_count_limits) {
   auto runtime = forge::asio::runtime{};
   const auto two_fields =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"first\"\r\n"
                  "\r\n"
                  "1\r\n"
                  "--demo\r\n"
                  "Content-Disposition: form-data; name=\"second\"\r\n"
                  "\r\n"
                  "2\r\n"
                  "--demo--\r\n"};
   auto part_limited = upload_reader{make_body_reader({two_fields}),
                                     upload_options{.memory_threshold_bytes = 256,
                                                    .max_file_bytes = 8,
                                                    .max_field_bytes = 8,
                                                    .max_total_bytes = 1024,
                                                    .max_parts = 1,
                                                    .max_files = 1,
                                                    .max_fields = 2}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, part_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);

   auto field_limited = upload_reader{make_body_reader({two_fields}),
                                      upload_options{.memory_threshold_bytes = 256,
                                                     .max_file_bytes = 8,
                                                     .max_field_bytes = 8,
                                                     .max_total_bytes = 1024,
                                                     .max_parts = 2,
                                                     .max_files = 1,
                                                     .max_fields = 1}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, field_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);

   const auto two_files =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"first\"; filename=\"first.txt\"\r\n"
                  "\r\n"
                  "a\r\n"
                  "--demo\r\n"
                  "Content-Disposition: form-data; name=\"second\"; filename=\"second.txt\"\r\n"
                  "\r\n"
                  "b\r\n"
                  "--demo--\r\n"};
   auto file_limited = upload_reader{make_body_reader({two_files}),
                                     upload_options{.memory_threshold_bytes = 256,
                                                    .max_file_bytes = 8,
                                                    .max_field_bytes = 8,
                                                    .max_total_bytes = 1024,
                                                    .max_parts = 2,
                                                    .max_files = 1,
                                                    .max_fields = 1}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, file_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_multipart_enforces_part_header_limits) {
   auto runtime = forge::asio::runtime{};
   const auto extra_header =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n"
                  "X-Test: value\r\n"
                  "\r\n"
                  "abc\r\n"
                  "--demo--\r\n"};
   auto count_limited = upload_reader{make_body_reader({extra_header}),
                                      upload_options{.memory_threshold_bytes = 256,
                                                     .max_file_bytes = 8,
                                                     .max_field_bytes = 8,
                                                     .max_total_bytes = 1024,
                                                     .max_parts = 1,
                                                     .max_files = 1,
                                                     .max_fields = 0,
                                                     .max_part_headers = 1,
                                                     .max_part_header_bytes = 1024}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, count_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);

   auto bytes_limited = upload_reader{make_body_reader({extra_header}),
                                      upload_options{.memory_threshold_bytes = 256,
                                                     .max_file_bytes = 8,
                                                     .max_field_bytes = 8,
                                                     .max_total_bytes = 1024,
                                                     .max_parts = 1,
                                                     .max_files = 1,
                                                     .max_fields = 0,
                                                     .max_part_headers = 8,
                                                     .max_part_header_bytes = 16}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, bytes_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_multipart_applies_file_limit_per_part) {
   auto runtime = forge::asio::runtime{};
   const auto two_files =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"first\"; filename=\"first.txt\"\r\n"
                  "\r\n"
                  "12345\r\n"
                  "--demo\r\n"
                  "Content-Disposition: form-data; name=\"second\"; filename=\"second.txt\"\r\n"
                  "\r\n"
                  "abcde\r\n"
                  "--demo--\r\n"};
   auto reader = upload_reader{make_body_reader({two_files}),
                               upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 5,
                                              .max_total_bytes = 1024}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));
   BOOST_REQUIRE_EQUAL(form.files.size(), 2U);
   BOOST_TEST(form.files[0].text() == "12345");
   BOOST_TEST(form.files[1].text() == "abcde");

   const auto oversized_file =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"large.txt\"\r\n"
                  "\r\n"
                  "123456\r\n"
                  "--demo--\r\n"};
   auto file_limited = upload_reader{make_body_reader({oversized_file}),
                                     upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 5,
                                                    .max_total_bytes = 1024}};
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, file_limited.async_read_multipart(
                                                    "multipart/form-data; boundary=demo")),
                     exceptions::payload_too_large);
}

BOOST_AUTO_TEST_CASE(http_upload_reader_parses_spooled_multipart_without_full_body_materialization) {
   auto files = temp_directory{};
   auto runtime = forge::asio::runtime{};
   const auto content = std::string{"0123456789abcdef0123456789abcdef"};
   auto reader = upload_reader{
      make_body_reader({
         "--demo\r\nContent-Disposition: form-data; name=\"file\"; filename=\"chunk.txt\"\r\n",
         "Content-Type: text/plain\r\n\r\n0123456789",
         "abcdef0123456789",
         "abcdef\r\n--demo--\r\n",
      }),
      upload_options{.memory_threshold_bytes = 8,
                     .max_file_bytes = 1024,
                     .max_field_bytes = 1024,
                     .max_total_bytes = 1024,
                     .spool_directory = files.path()}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));

   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   const auto& file = form.files.front();
   BOOST_TEST(file.name == "file");
   BOOST_REQUIRE(file.filename.has_value());
   BOOST_TEST(*file.filename == "chunk.txt");
   BOOST_TEST(!file.in_memory());
   BOOST_REQUIRE(file.spool.has_value());
   BOOST_TEST(std::filesystem::is_regular_file(file.spool->path()));
   BOOST_TEST(file.size == content.size());
   BOOST_TEST(file.text() == content);
}

BOOST_AUTO_TEST_CASE(http_upload_file_exposes_safe_filename_without_path_segments) {
   auto runtime = forge::asio::runtime{};
   const auto body =
      std::string{"--demo\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"..\\\\secret file.txt\"\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n"
                  "payload\r\n"
                  "--demo--\r\n"};
   auto reader = upload_reader{make_body_reader({body}),
                               upload_options{.memory_threshold_bytes = 256, .max_file_bytes = 1024,
                                              .max_total_bytes = 1024}};

   const auto form = forge::asio::blocking::run(
      runtime, reader.async_read_multipart("multipart/form-data; boundary=demo"));

   BOOST_REQUIRE_EQUAL(form.files.size(), 1U);
   BOOST_REQUIRE(form.files.front().filename.has_value());
   const auto raw_has_separator = form.files.front().filename->find('\\') != std::string::npos;
   BOOST_TEST(raw_has_separator);
   const auto safe = form.files.front().safe_filename();
   BOOST_REQUIRE(safe.has_value());
   BOOST_TEST(*safe == "secret_file.txt");
   BOOST_TEST(safe->find('/') == std::string::npos);
   BOOST_TEST(safe->find('\\') == std::string::npos);
}

BOOST_AUTO_TEST_CASE(connection_reconnects_after_connection_close) {
   auto runtime = forge::asio::runtime{};
   auto request_count = std::make_shared<std::atomic<int>>(0);

   auto server = forge::http::server{
       runtime,
       server_config{},
       [request_count](route_context& context) -> boost::asio::awaitable<response> {
          ++(*request_count);
          co_return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto connection = forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto first = make_request(method::get, "/close");
   first.keep_alive(false);
   first.set(field::connection, "close");

   const auto first_response = forge::asio::blocking::run(runtime, connection.async_request(std::move(first)));
   BOOST_TEST(first_response.result_int() == static_cast<unsigned>(status::ok));

   auto second = make_request(method::get, "/again");
   second.keep_alive(true);

   const auto second_response = forge::asio::blocking::run(runtime, connection.async_request(std::move(second)));
   BOOST_TEST(second_response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second_response.keep_alive());
   BOOST_TEST(request_count->load() == 2);
   BOOST_CHECK_GE(connection.metrics().reconnects, 1U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(connection_retries_only_idempotent_requests_after_remote_close) {
   auto retry_server = flaky_server{true};
   auto runtime = forge::asio::runtime{};
   auto connection =
       forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(retry_server.port()))};

   auto get_request = make_request(method::get, "/retry");
   const auto get_response =
      forge::asio::blocking::run(runtime,
                               connection.async_request(
                                  std::move(get_request),
                                  request_options{.retry_idempotent = true,
                                                  .max_retries = 1,
                                                  .retry_backoff = std::chrono::milliseconds{1}}));

   BOOST_TEST(get_response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(get_response.body() == "retry-ok");
   BOOST_CHECK_EQUAL(connection.metrics().retry_attempts, 1U);
   BOOST_CHECK_EQUAL(connection.metrics().completed_requests, 1U);

   auto no_retry_server = flaky_server{false};
   auto no_retry_connection =
       forge::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(no_retry_server.port()))};
   auto post_request = make_request(method::post, "/mutation");
   BOOST_CHECK_THROW(
       forge::asio::blocking::run(runtime,
                                no_retry_connection.async_request(
                                   std::move(post_request),
                                   request_options{.retry_idempotent = true,
                                                   .max_retries = 1,
                                                   .retry_backoff = std::chrono::milliseconds{1}})),
       std::exception);
   BOOST_CHECK_EQUAL(no_retry_connection.metrics().retry_attempts, 0U);
}

BOOST_AUTO_TEST_CASE(connection_serializes_concurrent_requests) {
   auto runtime = forge::asio::runtime{};
   auto request_count = std::make_shared<std::atomic<int>>(0);

   auto server = forge::http::server{
       runtime,
       server_config{},
       [request_count](route_context& context) -> boost::asio::awaitable<response> {
          ++(*request_count);
          co_return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto first = forge::asio::blocking::run(runtime, client.async_get("/one"));
   auto second = forge::asio::blocking::run(runtime, client.async_get("/two"));

   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(request_count->load() == 2);

   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_echo_shares_server_port) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto router = forge::http::router{};
   router.get("/health", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "ok");
   });
   router.websocket("/ws", [](forge::websocket::connection::ptr connection) {
      connection->on_message(
          [](forge::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
             co_await connection.send(std::move(message));
          });
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto http_client = forge::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   BOOST_TEST(forge::asio::blocking::run(runtime, http_client.async_get("/health")).body() == "ok");
   BOOST_TEST(forge::asio::blocking::run(runtime, http_client.async_get("/ws")).result_int() ==
              static_cast<unsigned>(status::upgrade_required));

   auto ws_client = forge::websocket::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto connection = ws_client.connect("/ws");

   auto received_mutex = std::mutex{};
   auto received_cv = std::condition_variable{};
   auto received = std::string{};
   auto received_ready = false;
   connection->on_message(
       [&received_mutex, &received_cv, &received,
        &received_ready](forge::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
          static_cast<void>(connection);
          {
             const auto lock = std::scoped_lock{received_mutex};
             received = std::move(message);
             received_ready = true;
          }
          received_cv.notify_all();
          co_return;
       });
   forge::asio::blocking::run(runtime, connection->send("hello"));

   {
      auto lock = std::unique_lock{received_mutex};
      BOOST_CHECK(received_cv.wait_for(lock, std::chrono::seconds{2}, [&received_ready] { return received_ready; }));
   }
   BOOST_TEST(received == "hello");
   forge::asio::blocking::run(runtime, connection->close());

   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_api_binding_strips_reserved_metadata) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto registry = forge::api::registry{};
   registry.install<api_cache>(api_cache::describe(), std::make_shared<routed_api_cache>());

   auto observed_peer = std::make_shared<std::string>();
   auto observed_public = std::make_shared<std::string>();
   auto plan = forge::api::binding()
                  .serve(registry)
                  .interceptor(forge::api::interceptor()
                                  .id("websocket-metadata")
                                  .phase(forge::api::interceptor_phase::authorize)
                                  .handler([observed_peer, observed_public](forge::api::call_context& context)
                                               -> boost::asio::awaitable<void> {
                                     *observed_peer = forge::api::metadata_value(
                                                        context.meta,
                                                        forge::api::p2p_remote_peer_metadata_key)
                                                        .value_or("missing");
                                     *observed_public =
                                        forge::api::metadata_value(context.meta, "x-client-trace")
                                           .value_or("missing");
                                     co_return;
                                  })
                                  .build())
                  .build();

   auto binding = forge::websocket::api().use(std::move(plan)).build();
   auto router = forge::http::router{};
   router.websocket("/api", [&runtime, binding = std::move(binding)](forge::websocket::connection::ptr connection) mutable {
      boost::asio::co_spawn(runtime.context(), binding.accept(std::move(connection)), boost::asio::detached);
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto ws_client = forge::websocket::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto connection = ws_client.connect("/api");

   auto response_mutex = std::mutex{};
   auto response_cv = std::condition_variable{};
   auto response = std::string{};
   auto response_ready = false;
   connection->on_message([&](forge::websocket::connection&, std::string message) -> boost::asio::awaitable<void> {
      {
         const auto lock = std::scoped_lock{response_mutex};
         response = std::move(message);
         response_ready = true;
      }
      response_cv.notify_all();
      co_return;
   });

   auto request = forge::api::frame{
       .kind = forge::api::frame_kind::request,
       .id = {.value = 71},
       .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
       .method = "read",
       .meta =
          {
             {.key = std::string{forge::api::p2p_remote_peer_metadata_key}, .value = "spoofed-peer"},
             {.key = "x-client-trace", .value = "trace-2"},
       },
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(api_read_chunk{}),
   };

   forge::asio::blocking::run(runtime, connection->send(pack_websocket_api_frame(request)));

   {
      auto lock = std::unique_lock{response_mutex};
      BOOST_CHECK(response_cv.wait_for(lock, std::chrono::seconds{2}, [&response_ready] {
         return response_ready;
      }));
   }

   const auto frame = unpack_websocket_api_frame(response);
   BOOST_CHECK(frame.kind == forge::api::frame_kind::response);
   BOOST_TEST(*observed_peer == "missing");
   BOOST_TEST(*observed_public == "trace-2");

   forge::asio::blocking::run(runtime, connection->close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_api_binding_dispatches_positional_method) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};

   auto registry = forge::api::registry{};
   registry.install<websocket_positional_api>(websocket_positional_api::describe(),
                                              std::make_shared<websocket_positional_impl>());
   auto binding = forge::websocket::api().use(forge::api::binding().serve(registry).build()).build();

   auto router = forge::http::router{};
   router.websocket("/api", [&runtime, binding = std::move(binding)](forge::websocket::connection::ptr connection) mutable {
      boost::asio::co_spawn(runtime.context(), binding.accept(std::move(connection)), boost::asio::detached);
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto ws_client = forge::websocket::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto connection = ws_client.connect("/api");

   auto response_mutex = std::mutex{};
   auto response_cv = std::condition_variable{};
   auto response = std::string{};
   auto response_ready = false;
   connection->on_message([&](forge::websocket::connection&, std::string message) -> boost::asio::awaitable<void> {
      {
         const auto lock = std::scoped_lock{response_mutex};
         response = std::move(message);
         response_ready = true;
      }
      response_cv.notify_all();
      co_return;
   });

   auto request = forge::api::frame{
       .kind = forge::api::frame_kind::request,
       .id = {.value = 72},
       .api = {.id = {"websocket.positional"}, .major = 1, .min_revision = 0},
       .method = "join",
       .codec = {.value = "forge.raw"},
       .payload = pack_api_payload(std::make_tuple(std::string{"left"}, std::string{"right"})),
   };

   forge::asio::blocking::run(runtime, connection->send(pack_websocket_api_frame(request)));

   {
      auto lock = std::unique_lock{response_mutex};
      BOOST_CHECK(response_cv.wait_for(lock, std::chrono::seconds{2}, [&response_ready] {
         return response_ready;
      }));
   }

   const auto frame = unpack_websocket_api_frame(response);
   BOOST_CHECK(frame.kind == forge::api::frame_kind::response);
   BOOST_TEST(forge::api::unpack_body<api_chunk>(frame.payload).bytes == "left:right:ws");

   forge::asio::blocking::run(runtime, connection->close());
   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_handler_exception_is_not_silently_swallowed) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto close_mutex = std::mutex{};
   auto close_cv = std::condition_variable{};
   auto close_called = false;

   auto router = forge::http::router{};
   router.websocket("/ws", [&](forge::websocket::connection::ptr connection) {
      connection->on_close([&](forge::websocket::connection&) {
         {
            const auto lock = std::scoped_lock{close_mutex};
            close_called = true;
         }
         close_cv.notify_all();
      });
      connection->on_message(
          [](forge::websocket::connection&, std::string) -> boost::asio::awaitable<void> {
             FORGE_THROW_EXCEPTION(forge::websocket::exceptions::malformed_frame, "bad websocket message");
          });
   });

   auto server = forge::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto ws_client = forge::websocket::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto connection = ws_client.connect("/ws");

   forge::asio::blocking::run(runtime, connection->send("bad"));

   {
      auto lock = std::unique_lock{close_mutex};
      BOOST_CHECK(close_cv.wait_for(lock, std::chrono::seconds{2}, [&close_called] { return close_called; }));
   }

   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_client_connects_over_tls) {
   auto echo_server = tls_websocket_echo_server{};
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto ws_client =
       forge::websocket::client{runtime, parse_base_url("wss://127.0.0.1:" + std::to_string(echo_server.port()))};

   auto connection = ws_client.connect("/secure", forge::websocket::client_options{.verify_peer = false});

   auto received_mutex = std::mutex{};
   auto received_cv = std::condition_variable{};
   auto received = std::string{};
   auto received_ready = false;
   connection->on_message(
       [&received_mutex, &received_cv, &received,
        &received_ready](forge::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
          static_cast<void>(connection);
          {
             const auto lock = std::scoped_lock{received_mutex};
             received = std::move(message);
             received_ready = true;
          }
          received_cv.notify_all();
          co_return;
       });

   forge::asio::blocking::run(runtime, connection->send("secure-hello"));
   {
      auto lock = std::unique_lock{received_mutex};
      BOOST_CHECK(received_cv.wait_for(lock, std::chrono::seconds{2}, [&received_ready] { return received_ready; }));
   }

   BOOST_TEST(received == "secure-hello");
   BOOST_CHECK_GE(connection->metrics().sent_messages, 1U);
   BOOST_CHECK_GE(connection->metrics().received_messages, 1U);
}

} // namespace
} // namespace forge::http
