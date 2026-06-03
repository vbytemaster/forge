#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>
#include <fcl/exceptions/macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

import fcl.api;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.http.api;
import fcl.http.base_url;
import fcl.http.client;
import fcl.http.connection;
import fcl.http.exceptions;
import fcl.http.middleware;
import fcl.http.route_context;
import fcl.http.router;
import fcl.http.server;
import fcl.http.target;
import fcl.http.types;
import fcl.raw.raw;
import fcl.websocket.client;
import fcl.websocket.connection;
import fcl.websocket.exceptions;

namespace fcl::http {
namespace {

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

BOOST_DESCRIBE_STRUCT(api_read_chunk, (), ())
BOOST_DESCRIBE_STRUCT(api_routed_read_chunk, (), (ref, offset, limit))
BOOST_DESCRIBE_STRUCT(api_chunk, (), (bytes))

class api_cache {
 public:
   virtual ~api_cache() = default;

   virtual boost::asio::awaitable<api_chunk> read(api_read_chunk request) = 0;
   virtual boost::asio::awaitable<api_chunk> routed_read(api_routed_read_chunk request) = 0;
   virtual boost::asio::awaitable<api_chunk> write(api_chunk request) = 0;

   static fcl::api::descriptor describe() {
      return fcl::api::contract<api_cache>({.id = {"cache"}, .version = {.major = 1, .revision = 8}})
          .method<&api_cache::read, api_read_chunk, api_chunk>("read")
          .error<api_errors::chunk_not_found>("chunk_not_found",
                                              {.status_code = fcl::api::status::not_found, .retryable = false})
          .method<&api_cache::routed_read, api_routed_read_chunk, api_chunk>("routed_read")
          .method<&api_cache::write, api_chunk, api_chunk>("write")
          .build();
   }
};

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

class flaky_http_server {
 public:
   explicit flaky_http_server(bool respond_to_retry) : respond_to_retry_(respond_to_retry), acceptor_(io_context_) {
      acceptor_.open(tcp::v4());
      acceptor_.set_option(asio::socket_base::reuse_address(true));
      acceptor_.bind(tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0});
      acceptor_.listen(asio::socket_base::max_listen_connections);
      port_ = acceptor_.local_endpoint().port();
      worker_ = std::thread([this] { run(); });
   }

   ~flaky_http_server() {
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
   router.get("/items/latest",
              [](route_context& context) { return make_text_response(context.request, status::ok, "static"); });
   router.get("/items/:id", [](route_context& context) {
      return make_text_response(context.request, status::ok, std::string{*context.route_param("id")});
   });

   auto static_request = make_request(method::get, "/items/latest?ignored=true");
   auto static_context = make_route_context(static_request);
   BOOST_TEST(router.handle(static_context).body() == "static");

   auto param_request = make_request(method::get, "/items/42");
   auto param_context = make_route_context(param_request);
   BOOST_TEST(router.handle(param_context).body() == "42");
}

BOOST_AUTO_TEST_CASE(router_returns_404_and_405) {
   auto router = fcl::http::router{};
   router.get("/items/:id",
              [](route_context& context) { return make_text_response(context.request, status::ok, "ok"); });

   auto missing_request = make_request(method::get, "/missing");
   auto missing_context = make_route_context(missing_request);
   BOOST_TEST(router.handle(missing_context).result_int() == static_cast<unsigned>(status::not_found));

   auto wrong_method_request = make_request(method::post, "/items/42");
   auto wrong_method_context = make_route_context(wrong_method_request);
   BOOST_TEST(router.handle(wrong_method_context).result_int() == static_cast<unsigned>(status::method_not_allowed));
}

BOOST_AUTO_TEST_CASE(router_maps_typed_http_exception_to_native_json_response) {
   auto router = fcl::http::router{};
   router.get("/missing", [](route_context&) -> response {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::not_found, "chunk not found");
   });

   auto request = make_request(method::get, "/missing");
   auto context = make_route_context(request);
   const auto response = router.handle(context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find(R"("error":"not_found")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("category":"fcl.http")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("code":404)") != std::string::npos);
   BOOST_TEST(response.body().find("chunk not found") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(router_rejects_duplicate_routes_before_serving) {
   auto router = fcl::http::router{};
   router.get("/items", [](route_context& context) { return make_text_response(context.request, status::ok, "one"); });

   BOOST_CHECK_THROW(
       router.get("/items",
                  [](route_context& context) { return make_text_response(context.request, status::ok, "two"); }),
       fcl::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(http_api_binding_maps_custom_exception_to_native_status) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache::describe(), std::make_shared<throwing_api_cache>());

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

   const auto response = router.handle(context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response[field::content_type] == "application/json");
   BOOST_TEST(response.body().find(R"("error":"chunk_not_found")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("category":"test.http.cache")") != std::string::npos);
   BOOST_TEST(response.body().find(R"("code":1)") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_binding_populates_get_request_from_route_and_query) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache::describe(), std::make_shared<routed_api_cache>());

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

   const auto response = router.handle(context);
   const auto response_bytes = fcl::api::bytes{response.body().begin(), response.body().end()};
   const auto unpacked = fcl::raw::unpack<api_chunk>(response_bytes);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(unpacked.bytes == "abc:7:4096");
}

BOOST_AUTO_TEST_CASE(http_api_binding_escapes_json_error_fields) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache::describe(), std::make_shared<escaping_api_cache>());

   auto router = fcl::http::router{};
   auto binding =
       fcl::http::api().use(fcl::api::binding().serve(apis).build())
           .get<&api_cache::read, api_read_chunk, api_chunk>("/cache/chunks/:ref")
           .build();
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;

   const auto response = router.handle(context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(response.body().find(R"(chunk \"missing\"\n\u0008\u0000not found)") != std::string::npos);
   BOOST_TEST(response.body().find('\n') == std::string::npos);
   BOOST_TEST(response.body().find('\b') == std::string::npos);
   BOOST_TEST(response.body().find('\0') == std::string::npos);
}

BOOST_AUTO_TEST_CASE(http_api_binding_passes_put_body_to_typed_api) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache::describe(), std::make_shared<throwing_api_cache>());

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

   const auto response = router.handle(context);
   const auto response_bytes = fcl::api::bytes{response.body().begin(), response.body().end()};
   const auto unpacked = fcl::raw::unpack<api_chunk>(response_bytes);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(unpacked.bytes == "from-put-body");
}

BOOST_AUTO_TEST_CASE(middleware_runs_in_order_and_can_short_circuit) {
   auto router = fcl::http::router{};
   auto trace = std::make_shared<std::string>();
   router.use([trace](route_context& context, next_handler next) {
      *trace += "a>";
      auto response = next();
      *trace += "<a";
      return response;
   });
   router.use([trace](route_context& context, next_handler next) {
      static_cast<void>(context);
      *trace += "b>";
      auto response = next();
      *trace += "<b";
      return response;
   });
   router.get("/ok", [](route_context& context) { return make_text_response(context.request, status::ok, "ok"); });

   auto ok_request = make_request(method::get, "/ok");
   auto ok_context = make_route_context(ok_request);
   BOOST_TEST(router.handle(ok_context).body() == "ok");
   BOOST_TEST(*trace == "a>b><b<a");

   auto short_router = fcl::http::router{};
   short_router.use([](route_context& context, next_handler next) {
      static_cast<void>(next);
      return make_text_response(context.request, status::unauthorized, "nope");
   });
   short_router.get("/secure", [](route_context& context) {
      return make_text_response(context.request, status::ok, "unreachable");
   });
   auto secure_request = make_request(method::get, "/secure");
   auto secure_context = make_route_context(secure_request);
   BOOST_TEST(short_router.handle(secure_context).result_int() == static_cast<unsigned>(status::unauthorized));
}

BOOST_AUTO_TEST_CASE(http_api_binding_mounts_ordered_middleware_contributions) {
   auto runtime = fcl::asio::runtime{};
   auto apis = fcl::api::registry{};
   apis.install<api_cache>(api_cache::describe(), std::make_shared<throwing_api_cache>());

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
                              [trace](route_context& context, next_handler next) {
                                 static_cast<void>(context);
                                 *trace += "limits>";
                                 auto response = next();
                                 *trace += "<limits";
                                 return response;
                              },
                      })
                      .middleware(fcl::http::middleware_descriptor{
                          .id = "auth",
                          .phase = fcl::http::middleware_phase::security,
                          .order = 100,
                          .path_prefix = "/cache",
                          .handler =
                              [trace](route_context& context, next_handler next) {
                                 static_cast<void>(context);
                                 *trace += "auth>";
                                 auto response = next();
                                 *trace += "<auth";
                                 return response;
                              },
                      })
                      .get<&api_cache::read, api_read_chunk, api_chunk>("/cache/chunks/:ref")
                      .build();

   auto router = fcl::http::router{};
   router.mount(binding);

   auto request = make_request(method::get, "/cache/chunks/abc");
   auto context = make_route_context(request);
   context.runtime = &runtime;
   const auto response = router.handle(context);

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::not_found));
   BOOST_TEST(*trace == "auth>limits><limits<auth");
}

BOOST_AUTO_TEST_CASE(http_api_binding_rejects_duplicate_middleware_ids) {
   auto duplicate = fcl::http::api()
                        .middleware(fcl::http::middleware_descriptor{
                            .id = "auth",
                            .handler = [](route_context& context, next_handler next) {
                               static_cast<void>(context);
                               return next();
                            },
                        })
                        .middleware(fcl::http::middleware_descriptor{
                            .id = "auth",
                            .handler = [](route_context& context, next_handler next) {
                               static_cast<void>(context);
                               return next();
                            },
                        })
                        .build();

   auto router = fcl::http::router{};
   BOOST_CHECK_THROW(router.mount(duplicate), fcl::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(middleware_exceptions_return_500) {
   auto router = fcl::http::router{};
   router.use([](route_context& context, next_handler next) -> response {
      static_cast<void>(context);
      static_cast<void>(next);
      throw std::runtime_error("boom");
   });
   router.get("/boom",
              [](route_context& context) { return make_text_response(context.request, status::ok, "unreachable"); });

   auto request = make_request(method::get, "/boom");
   auto context = make_route_context(request);
   BOOST_TEST(router.handle(context).result_int() == static_cast<unsigned>(status::internal_server_error));
}

BOOST_AUTO_TEST_CASE(client_roundtrips_over_local_server) {
   auto runtime = fcl::asio::runtime{};
   auto seen_target = std::make_shared<std::string>();
   auto seen_body = std::make_shared<std::string>();

   auto server = fcl::http::server{
       runtime,
       server_config{},
       [seen_target, seen_body](route_context& context) {
          *seen_target = std::string{context.request.target()};
          *seen_body = context.request.body();
          return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto client = fcl::http::client{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port) + "/spring")};

   const auto response = client.post_json("/v1/chain/get_info", R"({"ping":1})");

   BOOST_TEST(response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(response.body() == R"({"ok":true})");
   BOOST_TEST(*seen_target == "/spring/v1/chain/get_info");
   BOOST_TEST(*seen_body == R"({"ping":1})");
   BOOST_CHECK_EQUAL(client.metrics().completed_requests, 1U);
   BOOST_CHECK_EQUAL(client.metrics().status_2xx, 1U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(connection_reconnects_after_connection_close) {
   auto runtime = fcl::asio::runtime{};
   auto request_count = std::make_shared<std::atomic<int>>(0);

   auto server = fcl::http::server{
       runtime,
       server_config{},
       [request_count](route_context& context) {
          ++(*request_count);
          return make_json_response(context.request, R"({"ok":true})");
       },
   };
   server.start();

   const auto port = wait_for_port(server);
   auto connection = fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(port))};

   auto first = make_request(method::get, "/close");
   first.keep_alive(false);
   first.set(field::connection, "close");

   const auto first_response = connection.request(std::move(first));
   BOOST_TEST(first_response.result_int() == static_cast<unsigned>(status::ok));

   auto second = make_request(method::get, "/again");
   second.keep_alive(true);

   const auto second_response = connection.request(std::move(second));
   BOOST_TEST(second_response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(second_response.keep_alive());
   BOOST_TEST(request_count->load() == 2);
   BOOST_CHECK_GE(connection.metrics().reconnects, 1U);

   server.stop();
}

BOOST_AUTO_TEST_CASE(connection_retries_only_idempotent_requests_after_remote_close) {
   auto retry_server = flaky_http_server{true};
   auto runtime = fcl::asio::runtime{};
   auto connection =
       fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(retry_server.port()))};

   auto get_request = make_request(method::get, "/retry");
   const auto get_response = connection.request(
       std::move(get_request),
       request_options{.retry_idempotent = true, .max_retries = 1, .retry_backoff = std::chrono::milliseconds{1}});

   BOOST_TEST(get_response.result_int() == static_cast<unsigned>(status::ok));
   BOOST_TEST(get_response.body() == "retry-ok");
   BOOST_CHECK_EQUAL(connection.metrics().retry_attempts, 1U);
   BOOST_CHECK_EQUAL(connection.metrics().completed_requests, 1U);

   auto no_retry_server = flaky_http_server{false};
   auto no_retry_connection =
       fcl::http::connection{runtime, parse_base_url("http://127.0.0.1:" + std::to_string(no_retry_server.port()))};
   auto post_request = make_request(method::post, "/mutation");
   BOOST_CHECK_THROW(
       no_retry_connection.request(
           std::move(post_request),
           request_options{.retry_idempotent = true, .max_retries = 1, .retry_backoff = std::chrono::milliseconds{1}}),
       std::exception);
   BOOST_CHECK_EQUAL(no_retry_connection.metrics().retry_attempts, 0U);
}

BOOST_AUTO_TEST_CASE(connection_serializes_concurrent_requests) {
   auto runtime = fcl::asio::runtime{};
   auto request_count = std::make_shared<std::atomic<int>>(0);

   auto server = fcl::http::server{
       runtime,
       server_config{},
       [request_count](route_context& context) {
          ++(*request_count);
          return make_json_response(context.request, R"({"ok":true})");
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

BOOST_AUTO_TEST_CASE(websocket_echo_shares_http_server_port) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto router = fcl::http::router{};
   router.get("/health", [](route_context& context) { return make_text_response(context.request, status::ok, "ok"); });
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
   BOOST_TEST(http_client.get("/health").body() == "ok");
   BOOST_TEST(http_client.get("/ws").result_int() == static_cast<unsigned>(status::upgrade_required));

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
