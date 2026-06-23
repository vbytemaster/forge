module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.http.api.parameters;

import forge.api.exceptions;
import forge.http.body;
import forge.http.file;
import forge.http.stream;
import forge.http.types;
import forge.http.upload;
import forge.reflect.reflect;
import forge.raw.raw;

export namespace forge::http {

template <typename T> struct header {
   T value{};
   bool present = false;
};

template <typename T> struct query {
   T value{};
   bool present = false;
};

template <typename T> struct cookie {
   T value{};
   bool present = false;
};

template <typename T> struct body {
   T value{};
   bool present = false;
};

template <typename T> struct form {
   T value{};
   bool present = false;
};

template <typename T> struct form_field {
   T value{};
   bool present = false;
};

class body_stream {
 public:
   body_stream() = default;
   explicit body_stream(body_reader reader) : reader_{std::move(reader)} {}

   [[nodiscard]] bool valid() const noexcept {
      return reader_.valid();
   }

   boost::asio::awaitable<std::optional<body_chunk>> async_read() {
      co_return co_await reader_.async_read();
   }

   boost::asio::awaitable<std::string> async_read_all() {
      co_return co_await reader_.async_read_all();
   }

   [[nodiscard]] body_reader release_reader() noexcept {
      return std::move(reader_);
   }

   [[nodiscard]] std::uint64_t bytes_read() const noexcept {
      return reader_.bytes_read();
   }

 private:
   body_reader reader_;
};

struct body_bytes {
   std::vector<std::byte> bytes;
};

class upload_file {
 public:
   upload_file() = default;
   explicit upload_file(upload_part part) : part_{std::move(part)}, present_{true} {}

   [[nodiscard]] bool present() const noexcept {
      return present_;
   }

   [[nodiscard]] const upload_part& part() const noexcept {
      return part_;
   }

   [[nodiscard]] upload_part& part() noexcept {
      return part_;
   }

   [[nodiscard]] const std::string& name() const noexcept {
      return part_.name;
   }

   [[nodiscard]] const std::optional<std::string>& filename() const noexcept {
      return part_.filename;
   }

   [[nodiscard]] std::optional<std::string> safe_filename() const {
      return part_.safe_filename();
   }

   [[nodiscard]] const std::string& content_type() const noexcept {
      return part_.content_type;
   }

   [[nodiscard]] std::uint64_t size() const noexcept {
      return part_.size;
   }

   [[nodiscard]] bool in_memory() const noexcept {
      return part_.in_memory();
   }

   [[nodiscard]] std::string text() const {
      return part_.text();
   }

 private:
   upload_part part_;
   bool present_ = false;
};

struct bytes_response {
   std::vector<std::byte> bytes;
   std::string content_type = "application/octet-stream";
   status status_code = status::ok;
};

struct empty_response {
   status status_code = status::no_content;
};

namespace detail {

template <typename T> struct is_header : std::false_type {};
template <typename T> struct is_header<header<T>> : std::true_type {
   using value_type = T;
};

template <typename T> struct is_query : std::false_type {};
template <typename T> struct is_query<query<T>> : std::true_type {
   using value_type = T;
};

template <typename T> struct is_cookie : std::false_type {};
template <typename T> struct is_cookie<cookie<T>> : std::true_type {
   using value_type = T;
};

template <typename T> struct is_body : std::false_type {};
template <typename T> struct is_body<body<T>> : std::true_type {
   using value_type = T;
};

template <typename T> struct is_form : std::false_type {};
template <typename T> struct is_form<form<T>> : std::true_type {
   using value_type = T;
};

template <typename T> struct is_form_field : std::false_type {};
template <typename T> struct is_form_field<form_field<T>> : std::true_type {
   using value_type = T;
};

template <typename T>
inline constexpr auto is_body_stream_v = std::is_same_v<std::remove_cvref_t<T>, body_stream>;

template <typename T>
inline constexpr auto is_body_bytes_v = std::is_same_v<std::remove_cvref_t<T>, body_bytes>;

template <typename T>
inline constexpr auto is_upload_file_v = std::is_same_v<std::remove_cvref_t<T>, upload_file>;

template <typename T>
inline constexpr auto is_stream_response_v = std::is_same_v<std::remove_cvref_t<T>, stream_response>;

template <typename T>
inline constexpr auto is_streaming_response_v = std::is_same_v<std::remove_cvref_t<T>, streaming_response>;

template <typename T>
inline constexpr auto is_bytes_response_v = std::is_same_v<std::remove_cvref_t<T>, bytes_response>;

template <typename T>
inline constexpr auto is_empty_response_v = std::is_same_v<std::remove_cvref_t<T>, empty_response>;

template <typename T>
inline constexpr auto is_http_parameter_v =
   is_header<std::remove_cvref_t<T>>::value ||
   is_query<std::remove_cvref_t<T>>::value ||
   is_cookie<std::remove_cvref_t<T>>::value ||
   is_body<std::remove_cvref_t<T>>::value ||
   is_form<std::remove_cvref_t<T>>::value ||
   is_form_field<std::remove_cvref_t<T>>::value ||
   is_body_stream_v<std::remove_cvref_t<T>> ||
   is_body_bytes_v<std::remove_cvref_t<T>> ||
   is_upload_file_v<std::remove_cvref_t<T>>;

template <typename Object> struct parameter_member_predicate {
   template <typename Descriptor>
   using fn = std::bool_constant<
      is_http_parameter_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>>;
};

template <typename T, bool Described = forge::reflect::is_described_object_v<T>>
struct request_has_http_parameter : std::bool_constant<is_http_parameter_v<T>> {};

template <typename T> struct request_has_http_parameter<T, true> {
   using members = boost::describe::describe_members<std::remove_cvref_t<T>,
                                                     boost::describe::mod_any_access |
                                                        boost::describe::mod_inherited>;
   static constexpr auto value =
      boost::mp11::mp_any_of_q<members, parameter_member_predicate<T>>::value;
};

template <typename T>
inline constexpr auto request_has_http_parameter_v = request_has_http_parameter<T>::value;

template <typename Object> struct stream_member_predicate {
   template <typename Descriptor>
   using fn = std::bool_constant<
      is_body_stream_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>> ||
      is_body_bytes_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>> ||
      is_form<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>::value ||
      is_form_field<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>::value ||
      is_upload_file_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>>;
};

template <typename T, bool Described = forge::reflect::is_described_object_v<T>>
struct request_needs_stream : std::false_type {};

template <typename T> struct request_needs_stream<T, true> {
   using members = boost::describe::describe_members<std::remove_cvref_t<T>,
                                                     boost::describe::mod_any_access |
                                                        boost::describe::mod_inherited>;
   static constexpr auto value = boost::mp11::mp_any_of_q<members, stream_member_predicate<T>>::value;
};

template <typename T>
inline constexpr auto request_needs_stream_v = request_needs_stream<T>::value;

template <typename T>
inline constexpr auto response_needs_stream_v = std::is_same_v<std::remove_cvref_t<T>, file_response> ||
                                                is_stream_response_v<T> ||
                                                is_streaming_response_v<T>;

} // namespace detail

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const header<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP headers are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, header<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP headers are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const query<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP query parameters are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, query<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP query parameters are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const cookie<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP cookies are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, cookie<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP cookies are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const body<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP bodies are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, body<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP bodies are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const form<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP form fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, form<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP form fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const form_field<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP form fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, form_field<T>& value) {
   static_cast<void>(stream);
   static_cast<void>(value);
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                       "HTTP form fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const body_stream&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP body streams are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, body_stream&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP body streams are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const upload_file&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP upload files are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, upload_file&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP upload files are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const body_bytes&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP body bytes are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, body_bytes&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP body bytes are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const file_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP file responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, file_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP file responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const stream_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP stream responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, stream_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP stream responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const streaming_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP streaming responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, streaming_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP streaming responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const bytes_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP byte responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, bytes_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP byte responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const empty_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP empty responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, empty_response&) {
   FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error, "HTTP empty responses are HTTP-only and cannot use generic binary serialization");
}

} // namespace forge::http
