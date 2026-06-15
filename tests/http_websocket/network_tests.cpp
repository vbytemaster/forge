#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>
#include <fcl/api/macros.hpp>
#include <fcl/exceptions/macros.hpp>
#include <fcl/http/macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/awaitable.hpp>
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

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.http.api;
import fcl.http.base_url;
import fcl.http.body;
import fcl.http.client;
import fcl.http.connection;
import fcl.http.exceptions;
import fcl.http.file;
import fcl.http.mapping;
import fcl.http.middleware;
import fcl.http.proxy;
import fcl.http.range;
import fcl.http.route_context;
import fcl.http.router;
import fcl.http.server;
import fcl.http.stream;
import fcl.http.target;
import fcl.http.types;
import fcl.raw.raw;
import fcl.websocket.client;
import fcl.websocket.connection;
import fcl.websocket.exceptions;

namespace fcl::http {
namespace test_api {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

namespace api_errors {

enum class code : std::uint8_t {
   chunk_not_found = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "test.http.cache")

using chunk_not_found = fcl::exceptions::coded_exception<code, code::chunk_not_found>;

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

BOOST_DESCRIBE_STRUCT(api_read_chunk, (), ())
BOOST_DESCRIBE_STRUCT(api_routed_read_chunk, (), (ref, offset, limit))
BOOST_DESCRIBE_STRUCT(api_chunk, (), (bytes))
BOOST_DESCRIBE_STRUCT(macro_read_request, (), (ref, offset, limit))
BOOST_DESCRIBE_STRUCT(macro_write_request, (), (ref, bytes))
BOOST_DESCRIBE_STRUCT(macro_chunk, (), (bytes))

class api_cache : public fcl::api::contract<api_cache, fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~api_cache() = default;

   virtual boost::asio::awaitable<api_chunk> read(api_read_chunk request) = 0;
   virtual boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk request) = 0;
   virtual boost::asio::awaitable<api_chunk> write(api_chunk request) = 0;
};

class macro_cache : public fcl::api::contract<macro_cache, fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~macro_cache() = default;

   virtual boost::asio::awaitable<macro_chunk> read(macro_read_request request) = 0;
   virtual boost::asio::awaitable<macro_chunk> write(macro_write_request request) = 0;
};

} // namespace test_api
} // namespace fcl::http

FCL_API(::fcl::http::test_api::macro_cache, FCL_API_CONTRACT("cache.macro", 1, 0), FCL_API_METHOD(read),
        FCL_API_METHOD(write))

FCL_HTTP_API(::fcl::http::test_api::macro_cache,
             FCL_HTTP_GET(read, "/cache/chunks/:ref?offset={offset}&limit={limit}"),
             FCL_HTTP_PUT(write, "/cache/chunks/:ref", created))

namespace fcl::api {

template <> struct api_traits<::fcl::http::test_api::api_cache> {
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
      using api_cache = ::fcl::http::test_api::api_cache;
      return define<api_cache>(descriptor{.id = id(), .version = version(), .interface_type = typeid(api_cache)})
          .method<&api_cache::read, ::fcl::http::test_api::api_read_chunk, ::fcl::http::test_api::api_chunk>("read")
          .error<::fcl::http::test_api::api_errors::chunk_not_found>(
             "chunk_not_found", {.status_code = status::not_found, .retryable = false})
          .method<&api_cache::routed_read, ::fcl::http::test_api::api_routed_read_chunk,
                  ::fcl::http::test_api::api_chunk>("routed_read")
          .method<&api_cache::write, ::fcl::http::test_api::api_chunk, ::fcl::http::test_api::api_chunk>("write")
          .build();
   }
};

} // namespace fcl::api

namespace fcl::http {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

namespace api_errors = test_api::api_errors;
using test_api::api_cache;
using test_api::api_chunk;
using test_api::api_read_chunk;
using test_api::api_routed_read_chunk;
using test_api::macro_cache;
using test_api::macro_chunk;
using test_api::macro_read_request;
using test_api::macro_write_request;

[[nodiscard]] fcl::api::descriptor api_cache_descriptor() {
   return api_cache::describe();
}

class throwing_api_cache final : public api_cache {
 public:
   boost::asio::awaitable<api_chunk> read(api_read_chunk) override {
      FCL_THROW_EXCEPTION(api_errors::chunk_not_found, "chunk not found");
   }

   boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk) override {
      FCL_THROW_EXCEPTION(api_errors::chunk_not_found, "chunk not found");
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

class escaping_api_cache final : public api_cache {
 public:
   boost::asio::awaitable<api_chunk> read(api_read_chunk) override {
      FCL_THROW_EXCEPTION(api_errors::chunk_not_found, std::string{"chunk \"missing\"\n"} + '\b' + '\0' + "not found");
   }

   boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk) override {
      FCL_THROW_EXCEPTION(api_errors::chunk_not_found, std::string{"chunk \"missing\"\n"} + '\b' + '\0' + "not found");
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

class temp_directory {
 public:
   temp_directory() {
      path_ = std::filesystem::temp_directory_path() /
              ("fcl-http-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

response raw_http_exchange(std::uint16_t port, std::string request_text) {
   auto io_context = asio::io_context{};
   auto stream = beast::tcp_stream{io_context};
   stream.expires_after(std::chrono::seconds{2});
   stream.connect(tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});
   asio::write(stream.socket(), asio::buffer(request_text));

   auto buffer = beast::flat_buffer{};
   auto response_value = response{};
   boost::beast::http::read(stream, buffer, response_value);
   return response_value;
}

response handle(router& target, route_context& context) {
   if (context.runtime != nullptr) {
      return fcl::asio::blocking::run(*context.runtime, target.handle(context));
   }

   auto runtime = fcl::asio::runtime{};
   context.runtime = &runtime;
   auto result = fcl::asio::blocking::run(runtime, target.handle(context));
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
         auto request_value = request{};
         boost::beast::http::read(stream, buffer, request_value);

         auto response_value = make_text_response(request_value, status::ok, "retry-ok");
         response_value.keep_alive(false);
         boost::beast::http::write(stream, response_value);
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

BOOST_AUTO_TEST_CASE(router_matches_static_and_parameter_routes) {
   auto router = fcl::http::router{};
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
   auto router = fcl::http::router{};
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
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
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

   const auto response = fcl::asio::blocking::run(runtime, router.handle(context));
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "async-ok");
   BOOST_TEST(invoked->load());
}

BOOST_AUTO_TEST_CASE(router_awaits_async_middleware_chain) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
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

   const auto response = fcl::asio::blocking::run(runtime, router.handle(context));
   BOOST_TEST(response.body() == "ok");
   BOOST_TEST(*trace == "before><after");
}

BOOST_AUTO_TEST_CASE(router_maps_typed_http_exception_to_native_json_response) {
   auto router = fcl::http::router{};
   router.get("/missing", [](route_context&) -> boost::asio::awaitable<response> {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::not_found, "chunk not found");
   });

   auto request = make_request(method::get, "/missing");
   auto context = make_route_context(request);
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find(R"("error":"not_found")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("category":"fcl.http")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("code":404)") != std::string::npos);
   BOOST_TEST(response.body().find("chunk not found") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(router_rejects_duplicate_routes_before_serving) {
   auto router = fcl::http::router{};
   router.get("/items", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "one");
   });

   BOOST_CHECK_THROW(
       router.get("/items",
                  [](route_context& context) -> boost::asio::awaitable<response> {
                     co_return make_text_response(context.request, status::ok, "two");
                  }),
       fcl::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(http_api_binding_maps_custom_exception_to_native_status) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<throwing_api_cache>());

   auto router = fcl::http::router{};
   auto plan_builder = fcl::api::binding();
   plan_builder.serve(apis);
   auto builder = fcl::http::api(router);
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

BOOST_AUTO_TEST_CASE(http_api_binding_populates_get_request_from_route_and_query) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<routed_api_cache>());

   auto router = fcl::http::router{};
   auto binding = fcl::http::api()
                      .use(fcl::api::binding().serve(apis).build())
                      .get<&api_cache::routed_read, api_routed_read_chunk, api_chunk>(
                          "/cache/chunks/:ref", {.query = {"offset", "limit"}})
                      .build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc?offset=7&limit=4096");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto response_bytes = fcl::api::bytes{response.body().begin(), response.body().end()};
   const auto unpacked = fcl::raw::unpack<api_chunk>(response_bytes);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(unpacked.bytes == "abc:7:4096");
}

BOOST_AUTO_TEST_CASE(http_api_binding_escapes_json_error_fields) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<escaping_api_cache>());

   auto router = fcl::http::router{};
   auto binding =
       fcl::http::api().use(fcl::api::binding().serve(apis).build())
           .get<&api_cache::read, api_read_chunk, api_chunk>("/cache/chunks/:ref")
           .build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response.body().find(R"(chunk \"missing\"\n\u0008\u0000not found)") != std::string::npos);
   BOOST_TEST(response.body().find('\n') == std::string::npos);
   BOOST_TEST(response.body().find('\b') == std::string::npos);
   BOOST_TEST(response.body().find('\0') == std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_binding_passes_put_body_to_typed_api) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<throwing_api_cache>());

   auto router = fcl::http::router{};
   auto binding = fcl::http::api()
                      .use(fcl::api::binding().serve(apis).build())
                      .put<&api_cache::write, api_chunk, api_chunk>("/cache/chunks/:ref")
                      .build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   const auto body = fcl::raw::pack(api_chunk{.bytes = "from-put-body"});
   request.body().assign(body.begin(), body.end());
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto response_bytes = fcl::api::bytes{response.body().begin(), response.body().end()};
   const auto unpacked = fcl::raw::unpack<api_chunk>(response_bytes);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(unpacked.bytes == "from-put-body");
}

BOOST_AUTO_TEST_CASE(http_api_macro_describes_routes_for_fcl_api) {
   const auto routes = fcl::http::http_api_traits<macro_cache>::routes();

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
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = fcl::http::router{};
   auto binding = fcl::http::api().use(fcl::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc?offset=7&limit=4096");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto response_bytes = fcl::api::bytes{response.body().begin(), response.body().end()};
   const auto unpacked = fcl::api::unpack_body<macro_chunk>(response_bytes);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(unpacked.bytes == "abc:7:4096");
}

BOOST_AUTO_TEST_CASE(http_api_macro_put_rejects_body_route_disagreement) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = fcl::http::router{};
   auto binding = fcl::http::api().use(fcl::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto request = make_request(method::put, "/cache/chunks/abc");
   const auto body = fcl::api::pack_body(macro_write_request{.ref = "other", .bytes = "payload"});
   request.body().assign(body.begin(), body.end());
   request.prepare_payload();

   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::bad_request));
   BOOST_TEST(response.body().find("disagrees") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(typed_http_client_supports_handle_methods) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto apis = fcl::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto router = fcl::http::router{};
   auto binding = fcl::http::api().use(fcl::api::binding().serve(apis).build()).bind<macro_cache>().build();
   router.mount(binding);

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto cache = fcl::asio::blocking::run(runtime, fcl::http::remote<macro_cache>(client));
   auto chunk = fcl::asio::blocking::run(
      runtime, cache->read(macro_read_request{.ref = "abc", .offset = 3, .limit = 64}));
   auto receipt = fcl::asio::blocking::run(
      runtime, cache->write(macro_write_request{.ref = "abc", .bytes = "payload"}));

   BOOST_TEST(chunk.bytes == "abc:3:64");
   BOOST_TEST(receipt.bytes == "abc:payload");

   server.stop();
}

BOOST_AUTO_TEST_CASE(middleware_runs_in_order_and_can_short_circuit) {
   auto router = fcl::http::router{};
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

   auto short_router = fcl::http::router{};
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

BOOST_AUTO_TEST_CASE(http_api_binding_mounts_ordered_middleware_contributions) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache_descriptor(), std::make_shared<throwing_api_cache>());

   auto trace = std::make_shared<std::string>();
   auto plan = fcl::api::binding().serve(apis).build();
   auto binding = fcl::http::api()
                      .use(std::move(plan))
                      .middleware(fcl::http::middleware_descriptor{
                          .id = "limits",
                          .phase = fcl::http::middleware_phase::limits,
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
                      .middleware(fcl::http::middleware_descriptor{
                          .id = "auth",
                          .phase = fcl::http::middleware_phase::security,
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

   auto router = fcl::http::router{};
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = handle(router, context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(*trace == "auth>limits><limits<auth");
}

BOOST_AUTO_TEST_CASE(http_api_binding_mounts_under_base_path) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<macro_cache>(macro_cache::describe(), std::make_shared<macro_cache_impl>());

   auto trace = std::make_shared<std::string>();
   auto binding = fcl::http::api()
                      .use(fcl::api::binding().serve(apis).build())
                      .middleware(fcl::http::middleware_descriptor{
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

   auto router = fcl::http::router{};
   binding.mount(router, "/api/v1");

   auto request = make_request(method::get, "/api/v1/cache/chunks/abc?offset=7&limit=4096");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = handle(router, context);
   const auto response_bytes = fcl::api::bytes{response.body().begin(), response.body().end()};
   const auto unpacked = fcl::api::unpack_body<macro_chunk>(response_bytes);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(unpacked.bytes == "abc:7:4096");
   BOOST_TEST(*trace == "limit><limit");

   auto unprefixed_request = make_request(method::get, "/cache/chunks/abc?offset=7&limit=4096");
   auto unprefixed_context = make_route_context(unprefixed_request);
   unprefixed_context.runtime = &runtime;
   BOOST_TEST(handle(router, unprefixed_context).result_int() == static_cast<unsigned>(status::not_found));
}

BOOST_AUTO_TEST_CASE(http_api_binding_rejects_duplicate_middleware_ids) {
   auto duplicate = fcl::http::api()
                        .middleware(fcl::http::middleware_descriptor{
                            .id = "auth",
                            .handler = [](route_context& context,
                                          next_handler next) -> boost::asio::awaitable<response> {
                               static_cast<void>(context);
                               co_return co_await next();
                            },
                        })
                        .middleware(fcl::http::middleware_descriptor{
                            .id = "auth",
                            .handler = [](route_context& context,
                                          next_handler next) -> boost::asio::awaitable<response> {
                               static_cast<void>(context);
                               co_return co_await next();
                            },
                        })
                        .build();

   auto router = fcl::http::router{};
   BOOST_CHECK_THROW(router.mount(duplicate), fcl::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(middleware_exceptions_return_500) {
   auto router = fcl::http::router{};
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
   auto runtime = fcl::asio::runtime{};
   auto seen_target = std::make_shared<std::string>();
   auto seen_body = std::make_shared<std::string>();

   auto server = fcl::http::server{
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
   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port) + "/api")};

   const auto response = fcl::asio::blocking::run(runtime, client.async_post_json("/v1/info", R"({"ping":1})"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == R"({"ok":true})");
   BOOST_TEST(*seen_target == "/api/v1/info");
   BOOST_TEST(*seen_body == R"({"ping":1})");
   BOOST_CHECK_EQUAL(client.metrics().completed_requests, 1U);
   BOOST_CHECK_EQUAL(client.metrics().status_2xx, 1U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(server_async_start_binds_before_return) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = fcl::http::server{
      runtime,
      server_config{},
      [](route_context& context) -> boost::asio::awaitable<response> {
         co_return make_text_response(context.request, status::ok, "ready");
      },
   };

   fcl::asio::blocking::run(runtime, server.async_start());

   BOOST_TEST(server.port() != 0U);
   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = fcl::asio::blocking::run(runtime, client.async_post_json("/health", "{}"));
   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "ready");

   fcl::asio::blocking::run(runtime, server.async_stop());
   BOOST_TEST(server.port() == 0U);
}

BOOST_AUTO_TEST_CASE(server_rejects_request_body_over_configured_limit) {
   auto runtime = fcl::asio::runtime{};
   auto invoked = std::make_shared<std::atomic<bool>>(false);
   auto server = fcl::http::server{
      runtime,
      server_config{.max_request_body_bytes = 4},
      [invoked](route_context& context) -> boost::asio::awaitable<response> {
         *invoked = true;
         co_return make_text_response(context.request, status::ok, "unreachable");
      },
   };
   fcl::asio::blocking::run(runtime, server.async_start());

   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request = make_request(method::post, "/upload");
   request.body() = "12345";
   request.prepare_payload();

   const auto response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(request)));
   BOOST_TEST(response.result_int() == 413);
   BOOST_TEST(!invoked->load());

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_route_reads_large_request_body_in_chunks) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto total_bytes = std::make_shared<std::atomic<std::size_t>>(0);
   auto chunk_count = std::make_shared<std::atomic<std::size_t>>(0);
   auto header_body_empty = std::make_shared<std::atomic<bool>>(false);

   auto router = fcl::http::router{};
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

   auto server = fcl::http::server{runtime, server_config{.max_request_body_bytes = 512 * 1024}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::post, "/upload");
   request_value.body().assign(256 * 1024, 'x');
   request_value.prepare_payload();

   const auto response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == std::to_string(256 * 1024));
   BOOST_TEST(total_bytes->load() == 256U * 1024U);
   BOOST_TEST(chunk_count->load() > 1U);
   BOOST_TEST(header_body_empty->load());

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_route_writes_chunked_response_body) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto produced = std::make_shared<std::atomic<std::size_t>>(0);

   auto router = fcl::http::router{};
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

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = fcl::asio::blocking::run(runtime, client.async_get("/download"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "alpha-omega");
   BOOST_TEST(response[field::transfer_encoding] == "chunked");
   BOOST_TEST(produced->load() == 3U);

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_middleware_short_circuits_before_body_read) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = fcl::http::router{};
   router.use([](route_context& context, next_handler next) -> boost::asio::awaitable<response> {
      static_cast<void>(next);
      co_return make_text_response(context.request, status::unauthorized, "blocked");
   });
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      static_cast<void>(request_value);
      invoked->store(true);
      co_return stream_response::buffered(response{status::ok, 11});
   });

   auto server = fcl::http::server{runtime, server_config{.read_timeout = std::chrono::seconds{5}}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   const auto response = raw_http_exchange(
      server.port(),
      "POST /upload HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 1048576\r\n\r\n");

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::unauthorized));
   BOOST_TEST(response.body() == "blocked");
   BOOST_TEST(!invoked->load());

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_stream_body_limit_fires_during_stream_read) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto invoked = std::make_shared<std::atomic<bool>>(false);

   auto router = fcl::http::router{};
   router.post_stream("/upload", [invoked](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      invoked->store(true);
      while (co_await request_value.body.async_read()) {
      }
      co_return stream_response::buffered(make_text_response(request_value.context.request, status::ok, "ok"));
   });

   auto server = fcl::http::server{runtime, server_config{.max_request_body_bytes = 4}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

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

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_serves_full_file_stream) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path(), file_options{.content_type = "application/test"});

   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   const auto response = fcl::asio::blocking::run(runtime, client.async_get("/files/chunk.bin"));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == "0123456789");
   BOOST_TEST(response[field::content_type] == "application/test");
   BOOST_TEST(response[field::accept_ranges] == "bytes");
   BOOST_TEST(response[field::etag].size() > 0U);
   BOOST_TEST(response[field::last_modified].size() > 0U);

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_serves_byte_range) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::get, "/files/chunk.bin");
   request_value.set(field::range, "bytes=2-5");

   const auto response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::partial_content));
   BOOST_TEST(response.body() == "2345");
   BOOST_TEST(response[field::content_range] == "bytes 2-5/10");
   BOOST_TEST(response[field::content_length] == "4");

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_rejects_invalid_range) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
   router.get_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::get, "/files/chunk.bin");
   request_value.set(field::range, "bytes=20-30");

   const auto response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::range_not_satisfiable));
   BOOST_TEST(response.body().empty());
   BOOST_TEST(response[field::content_range] == "bytes */10");

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_head_returns_headers_without_body) {
   auto files = temp_directory{};
   files.write("chunk.bin", "0123456789");
   auto root = std::make_shared<static_file_root>(files.path());

   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
   router.head_stream("/files/:name", [root](stream_request& request_value) -> boost::asio::awaitable<stream_response> {
      co_return co_await root->serve(request_value, *request_value.context.route_param("name"));
   });

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   fcl::asio::blocking::run(runtime, server.async_start());

   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(server.port()))};
   auto request_value = make_request(method::head, "/files/chunk.bin");

   const auto response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(request_value)));

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body().empty());
   BOOST_TEST(response[field::content_length] == "10");
   BOOST_TEST(response[field::accept_ranges] == "bytes");

   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(http_static_file_root_rejects_traversal_and_symlink) {
   auto files = temp_directory{};
   files.write("visible.bin", "visible");
   auto outside = std::filesystem::temp_directory_path() / "fcl-http-outside-secret.bin";
   {
      auto output = std::ofstream{outside, std::ios::binary};
      output << "secret";
   }
   const auto link = files.path() / "link.bin";
   std::error_code symlink_error;
   std::filesystem::create_symlink(outside, link, symlink_error);

   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto root = static_file_root{files.path()};
   auto request_value = make_request(method::get, "/files/link.bin");
   auto context = make_route_context(request_value);
   auto stream_request_value = stream_request{.context = context, .body = body_reader{}};

   const auto traversal = fcl::asio::blocking::run(runtime, root.serve(stream_request_value, "../secret.bin"));
   BOOST_TEST(traversal.head.result_int() == static_cast<unsigned>(status::forbidden));

   if (!symlink_error) {
      const auto symlink = fcl::asio::blocking::run(runtime, root.serve(stream_request_value, "link.bin"));
      BOOST_TEST(symlink.head.result_int() == static_cast<unsigned>(status::forbidden));
   }

   std::error_code ignored;
   std::filesystem::remove(outside, ignored);
}

BOOST_AUTO_TEST_CASE(connection_reconnects_after_connection_close) {
   auto runtime = fcl::asio::runtime{};
   auto request_count = std::make_shared<std::atomic<int>>(0);

   auto server = fcl::http::server{
       runtime,
       server_config{},
       [request_count](route_context& context) -> boost::asio::awaitable<response> {
          ++(*request_count);
          co_return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto first = make_request(method::get, "/close");
   first.keep_alive(false);
   first.set(field::connection, "close");

   const auto first_response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(first)));
   BOOST_TEST(first_response.result_int() == static_cast<unsigned>(status::ok));

   auto second = make_request(method::get, "/again");
   second.keep_alive(true);

   const auto second_response = fcl::asio::blocking::run(runtime, connection.async_request(std::move(second)));
   BOOST_TEST(second_response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second_response.keep_alive());
   BOOST_TEST(request_count->load() == 2);
   BOOST_CHECK_GE(connection.metrics().reconnects, 1U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(connection_retries_only_idempotent_requests_after_remote_close) {
   auto retry_server = flaky_server{true};
   auto runtime = fcl::asio::runtime{};
   auto connection =
       fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(retry_server.port()))};

   auto get_request = make_request(method::get, "/retry");
   const auto get_response =
      fcl::asio::blocking::run(runtime,
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
       fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(no_retry_server.port()))};
   auto post_request = make_request(method::post, "/mutation");
   BOOST_CHECK_THROW(
       fcl::asio::blocking::run(runtime,
                                no_retry_connection.async_request(
                                   std::move(post_request),
                                   request_options{.retry_idempotent = true,
                                                   .max_retries = 1,
                                                   .retry_backoff = std::chrono::milliseconds{1}})),
       std::exception);
   BOOST_CHECK_EQUAL(no_retry_connection.metrics().retry_attempts, 0U);
}

BOOST_AUTO_TEST_CASE(connection_serializes_concurrent_requests) {
   auto runtime = fcl::asio::runtime{};
   auto request_count = std::make_shared<std::atomic<int>>(0);

   auto server = fcl::http::server{
       runtime,
       server_config{},
       [request_count](route_context& context) -> boost::asio::awaitable<response> {
          ++(*request_count);
          co_return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto first = fcl::asio::blocking::run(runtime, client.async_get("/one"));
   auto second = fcl::asio::blocking::run(runtime, client.async_get("/two"));

   BOOST_TEST(first.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(request_count->load() == 2);

   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_echo_shares_server_port) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
   router.get("/health", [](route_context& context) -> boost::asio::awaitable<response> {
      co_return make_text_response(context.request, status::ok, "ok");
   });
   router.websocket("/ws", [](fcl::websocket::connection::ptr connection) {
      connection->on_message(
          [](fcl::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
             co_await connection.send(std::move(message));
          });
   });

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto http_client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   BOOST_TEST(fcl::asio::blocking::run(runtime, http_client.async_get("/health")).body() == "ok");
   BOOST_TEST(fcl::asio::blocking::run(runtime, http_client.async_get("/ws")).result_int() ==
              static_cast<unsigned>(status::upgrade_required));

   auto ws_client = fcl::websocket::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto connection = ws_client.connect("/ws");

   auto received_mutex = std::mutex{};
   auto received_cv = std::condition_variable{};
   auto received = std::string{};
   auto received_ready = false;
   connection->on_message(
       [&received_mutex, &received_cv, &received,
        &received_ready](fcl::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
          static_cast<void>(connection);
          {
             const auto lock = std::scoped_lock{received_mutex};
             received = std::move(message);
             received_ready = true;
          }
          received_cv.notify_all();
          co_return;
       });
   fcl::asio::blocking::run(runtime, connection->send("hello"));

   {
      auto lock = std::unique_lock{received_mutex};
      BOOST_CHECK(received_cv.wait_for(lock, std::chrono::seconds{2}, [&received_ready] { return received_ready; }));
   }
   BOOST_TEST(received == "hello");
   fcl::asio::blocking::run(runtime, connection->close());

   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_handler_exception_is_not_silently_swallowed) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto close_mutex = std::mutex{};
   auto close_cv = std::condition_variable{};
   auto close_called = false;

   auto router = fcl::http::router{};
   router.websocket("/ws", [&](fcl::websocket::connection::ptr connection) {
      connection->on_close([&](fcl::websocket::connection&) {
         {
            const auto lock = std::scoped_lock{close_mutex};
            close_called = true;
         }
         close_cv.notify_all();
      });
      connection->on_message(
          [](fcl::websocket::connection&, std::string) -> boost::asio::awaitable<void> {
             FCL_THROW_EXCEPTION(fcl::websocket::exceptions::malformed_frame, "bad websocket message");
          });
   });

   auto server = fcl::http::server{runtime, server_config{}, std::move(router)};
   server.start();

   const auto port = wait_for_port(server);
   auto ws_client = fcl::websocket::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};
   auto connection = ws_client.connect("/ws");

   fcl::asio::blocking::run(runtime, connection->send("bad"));

   {
      auto lock = std::unique_lock{close_mutex};
      BOOST_CHECK(close_cv.wait_for(lock, std::chrono::seconds{2}, [&close_called] { return close_called; }));
   }

   server.stop();
}

BOOST_AUTO_TEST_CASE(websocket_client_connects_over_tls) {
   auto echo_server = tls_websocket_echo_server{};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto ws_client =
       fcl::websocket::client{runtime, parse_base_url("wss://127.0.0.1:" + std::to_string(echo_server.port()))};

   auto connection = ws_client.connect("/secure", fcl::websocket::client_options{.verify_peer = false});

   auto received_mutex = std::mutex{};
   auto received_cv = std::condition_variable{};
   auto received = std::string{};
   auto received_ready = false;
   connection->on_message(
       [&received_mutex, &received_cv, &received,
        &received_ready](fcl::websocket::connection& connection, std::string message) -> boost::asio::awaitable<void> {
          static_cast<void>(connection);
          {
             const auto lock = std::scoped_lock{received_mutex};
             received = std::move(message);
             received_ready = true;
          }
          received_cv.notify_all();
          co_return;
       });

   fcl::asio::blocking::run(runtime, connection->send("secure-hello"));
   {
      auto lock = std::unique_lock{received_mutex};
      BOOST_CHECK(received_cv.wait_for(lock, std::chrono::seconds{2}, [&received_ready] { return received_ready; }));
   }

   BOOST_TEST(received == "secure-hello");
   BOOST_CHECK_GE(connection->metrics().sent_messages, 1U);
   BOOST_CHECK_GE(connection->metrics().received_messages, 1U);
}

} // namespace
} // namespace fcl::http
