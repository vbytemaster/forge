#pragma once

#include <memory>

namespace forge::http {

class body_reader;
class route_context;
struct stream_request;
struct stream_response;

namespace detail {

struct stream_server_access {
   static body_reader mark_request_body(body_reader body, std::shared_ptr<const void> request_body_marker);
   static std::shared_ptr<const void> continue_before_response_marker(const body_reader& body) noexcept;
   static stream_request make_request(route_context& context,
                                      body_reader body,
                                      std::shared_ptr<const void> request_body_marker);
   static bool response_body_uses_request(const stream_response& response_value,
                                          const std::shared_ptr<const void>& request_body_marker) noexcept;
};

} // namespace detail
} // namespace forge::http
