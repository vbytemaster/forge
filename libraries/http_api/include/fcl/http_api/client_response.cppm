module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <tuple>
#include <utility>
#include <vector>

export module fcl.http.api.client_response;

import fcl.api.connection;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.types;
import fcl.http.body;
import fcl.http.api.client_request;
import fcl.http.api.parameters;
export import fcl.http.client;
import fcl.http.exceptions;
import fcl.http.file;
export import fcl.http.api.mapping;
import fcl.http.stream;
import fcl.http.types;
import fcl.http.upload;
import fcl.json;
import fcl.reflect.reflect;

export namespace fcl::http::api {

namespace detail {

[[nodiscard]] inline fcl::api::error_payload decode_error_payload(const response& value) {
   auto decoded = fcl::json::read<fcl::api::error_payload>(
      value.body(), fcl::json::read_options{.source_name = "http.error",
                                            .unknown_fields = fcl::json::unknown_field_policy::ignore});
   if (decoded.ok()) {
      auto payload = std::move(decoded.value);
      payload.status_code = static_cast<fcl::api::status>(value.result_int());
      return payload;
   }
   return fcl::api::error_payload{
      .error = "http_error",
      .message = value.body().empty() ? "HTTP API request failed" : value.body(),
      .retryable = false,
      .status_code = static_cast<fcl::api::status>(value.result_int()),
      .identity =
         {
            .category = "fcl.api",
            .code = static_cast<std::uint32_t>(fcl::api::exceptions::code::remote_internal),
         },
   };
}

inline constexpr auto max_stream_error_body_bytes = std::uint64_t{64U * 1024U};

boost::asio::awaitable<std::string> read_bounded_error_body(body_reader& body) {
   auto output = std::string{};
   while (auto chunk = co_await body.async_read()) {
      if (chunk->bytes.size() > max_stream_error_body_bytes - output.size()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::payload_too_large,
                             "HTTP API error response body exceeds the streaming client limit");
      }
      output.append(reinterpret_cast<const char*>(chunk->bytes.data()), chunk->bytes.size());
   }
   co_return output;
}

template <typename Request, typename Response>
boost::asio::awaitable<Response> call(client& target, const fcl::api::descriptor& descriptor,
                                      const route& route, Request value) {
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);

      auto response_value = body.has_value()
         ? co_await target.async_stream_request(std::move(request_value), std::move(*body))
         : co_await target.async_stream_request(std::move(request_value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = decode_error_payload(response_value.head);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else if constexpr (detail::is_bytes_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      auto bytes = std::vector<std::byte>(response_value.body().size());
      if (!bytes.empty()) {
         std::memcpy(bytes.data(), response_value.body().data(), response_value.body().size());
      }
      auto content_type = std::string{};
      if (auto iterator = response_value.find(field::content_type); iterator != response_value.end()) {
         content_type = std::string{iterator->value()};
      }
      co_return Response{
         .bytes = std::move(bytes),
         .content_type = content_type.empty() ? std::string{"application/octet-stream"} : std::move(content_type),
         .status_code = response_value.result(),
      };
   } else if constexpr (detail::is_empty_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      co_return Response{.status_code = response_value.result()};
   } else {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      auto decoded = fcl::json::read<Response>(response_value.body(),
                                               fcl::json::read_options{.source_name = "http.response",
                                                                       .unknown_fields =
                                                                          fcl::json::unknown_field_policy::error});
      if (!decoded.ok()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API response JSON is invalid");
      }
      co_return std::move(decoded.value);
   }
}

template <typename Tuple, typename Response>
boost::asio::awaitable<Response> call_arguments(client& target,
                                                const fcl::api::descriptor& descriptor,
                                                const route& route,
                                                Tuple value,
                                                const std::vector<std::string>& argument_names) {
   reject_http_positional_parameters(value);
   auto request_parts = make_client_request(target, route, value, argument_names);
   auto request_body = bind_positional_request_body(request_parts.value, route, value, request_parts.consumed);
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto response_value = request_body.has_value()
         ? co_await target.async_stream_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_stream_request(std::move(request_parts.value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = decode_error_payload(response_value.head);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else {
      auto response_value = request_body.has_value()
         ? co_await target.async_streaming_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_request(std::move(request_parts.value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (detail::is_bytes_response_v<Response>) {
         auto bytes = std::vector<std::byte>(response_value.body().size());
         if (!bytes.empty()) {
            std::memcpy(bytes.data(), response_value.body().data(), response_value.body().size());
         }
         auto content_type = std::string{};
         if (auto iterator = response_value.find(field::content_type); iterator != response_value.end()) {
            content_type = std::string{iterator->value()};
         }
         co_return Response{
            .bytes = std::move(bytes),
            .content_type = content_type.empty() ? std::string{"application/octet-stream"} : std::move(content_type),
            .status_code = response_value.result(),
         };
      } else if constexpr (detail::is_empty_response_v<Response>) {
         co_return Response{.status_code = response_value.result()};
      } else {
         auto decoded = fcl::json::read<Response>(
            response_value.body(),
            fcl::json::read_options{.source_name = "http.response",
                                    .unknown_fields = fcl::json::unknown_field_policy::error});
         if (!decoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API response JSON is invalid");
         }
         co_return std::move(decoded.value);
      }
   }
}

} // namespace detail

} // namespace fcl::http::api
