#include "details/stream_server_access.hxx"

#include <utility>

import forge.http.stream;

namespace forge::http {

body_reader detail::stream_server_access::mark_request_body(body_reader body,
                                                            std::shared_ptr<const void> request_body_marker) {
   return body_reader{std::move(body.source_), std::move(request_body_marker)};
}

std::shared_ptr<const void> detail::stream_server_access::continue_before_response_marker(
   const body_reader& body) noexcept {
   return body.continue_before_response_marker();
}

stream_request detail::stream_server_access::make_request(route_context& context,
                                                          body_reader body,
                                                          std::shared_ptr<const void> request_body_marker) {
   return stream_request{context, std::move(body), std::move(request_body_marker)};
}

bool detail::stream_server_access::response_body_uses_request(
   const stream_response& response_value, const std::shared_ptr<const void>& request_body_marker) noexcept {
   return request_body_marker && response_value.body.request_body_marker_ == request_body_marker;
}

} // namespace forge::http
