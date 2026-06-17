module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <fcl/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.http.binding;

import fcl.api.exceptions;
import fcl.http.body;
import fcl.http.file;
import fcl.http.stream;
import fcl.http.types;
import fcl.http.upload;
import fcl.reflect.reflect;

export namespace fcl::http {

template <typename T> struct header {
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

template <typename Object> struct stream_member_predicate {
   template <typename Descriptor>
   using fn = std::bool_constant<
      is_body_stream_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>> ||
      is_body_bytes_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>> ||
      is_form_field<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>::value ||
      is_upload_file_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>>;
};

template <typename T, bool Described = fcl::reflect::is_described_object_v<T>>
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

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const header<T>&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP header fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, header<T>&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP header fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator<<(Stream& stream, const form_field<T>&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP form fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream, typename T> Stream& operator>>(Stream& stream, form_field<T>&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP form fields are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const body_stream&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP body streams are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, body_stream&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP body streams are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const upload_file&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP upload files are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, upload_file&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP upload files are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const body_bytes&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP body bytes are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, body_bytes&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP body bytes are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const file_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP file responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, file_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP file responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const stream_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP stream responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, stream_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP stream responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const streaming_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP streaming responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, streaming_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP streaming responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const bytes_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP byte responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, bytes_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP byte responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator<<(Stream& stream, const empty_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP empty responses are HTTP-only and cannot use generic binary serialization");
}

template <typename Stream> Stream& operator>>(Stream& stream, empty_response&) {
   FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "HTTP empty responses are HTTP-only and cannot use generic binary serialization");
}

} // namespace fcl::http
