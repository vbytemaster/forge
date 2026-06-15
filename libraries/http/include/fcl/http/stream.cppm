module;

#include <functional>
#include <optional>
#include <utility>

#include <boost/asio/awaitable.hpp>

export module fcl.http.stream;

import fcl.http.body;
import fcl.http.route_context;
import fcl.http.types;

export namespace fcl::http {

struct stream_request {
   route_context& context;
   body_reader body;
};

struct stream_response {
   using body_source = std::function<boost::asio::awaitable<std::optional<body_chunk>>()>;

   response head;
   body_source body;

   [[nodiscard]] static stream_response buffered(response response_value) {
      return stream_response{.head = std::move(response_value), .body = {}};
   }
};

using stream_route_handler = std::function<boost::asio::awaitable<stream_response>(stream_request&)>;

} // namespace fcl::http
