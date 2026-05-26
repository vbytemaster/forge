module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fcl/exception/macros.hpp>

export module fcl.quic.exceptions;

export import fcl.exception.exception;

export namespace fcl::quic::exceptions {

enum class code : std::uint16_t {
   invalid_endpoint = 1,
   invalid_options = 2,
   dependency_unavailable = 3,
   connect_timeout = 4,
   handshake_timeout = 5,
   idle_timeout = 6,
   tls_failed = 7,
   peer_verification_failed = 8,
   alpn_mismatch = 9,
   frame_too_large = 10,
   malformed_frame = 11,
   backpressure_rejected = 12,
   connection_closed = 13,
   stream_closed = 14,
   stream_reset = 15,
   canceled = 16,
   unsupported = 17,
   internal = 18,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.quic")

using invalid_endpoint = fcl::exception::coded_exception<code, code::invalid_endpoint>;
using invalid_options = fcl::exception::coded_exception<code, code::invalid_options>;
using dependency_unavailable = fcl::exception::coded_exception<code, code::dependency_unavailable>;
using connect_timeout = fcl::exception::coded_exception<code, code::connect_timeout>;
using handshake_timeout = fcl::exception::coded_exception<code, code::handshake_timeout>;
using idle_timeout = fcl::exception::coded_exception<code, code::idle_timeout>;
using tls_failed = fcl::exception::coded_exception<code, code::tls_failed>;
using peer_verification_failed = fcl::exception::coded_exception<code, code::peer_verification_failed>;
using alpn_mismatch = fcl::exception::coded_exception<code, code::alpn_mismatch>;
using frame_too_large = fcl::exception::coded_exception<code, code::frame_too_large>;
using malformed_frame = fcl::exception::coded_exception<code, code::malformed_frame>;
using backpressure_rejected = fcl::exception::coded_exception<code, code::backpressure_rejected>;
using connection_closed = fcl::exception::coded_exception<code, code::connection_closed>;
using stream_closed = fcl::exception::coded_exception<code, code::stream_closed>;
using stream_reset = fcl::exception::coded_exception<code, code::stream_reset>;
using canceled = fcl::exception::coded_exception<code, code::canceled>;
using unsupported = fcl::exception::coded_exception<code, code::unsupported>;
using internal = fcl::exception::coded_exception<code, code::internal>;

[[nodiscard]] inline std::optional<code> code_of(const fcl::exception::base& error) noexcept {
   const auto& value = error.code();
   if (!value || std::string_view{value.category().name()} != "fcl.quic") {
      return std::nullopt;
   }
   return static_cast<code>(value.value());
}

[[nodiscard]] inline bool is(const fcl::exception::base& error, code value) noexcept {
   return error.code() == fcl::exception::make_error_code(value);
}

[[noreturn]] inline void raise(code value, std::string message) {
   switch (value) {
   case code::invalid_endpoint:
      throw invalid_endpoint{std::move(message)};
   case code::invalid_options:
      throw invalid_options{std::move(message)};
   case code::dependency_unavailable:
      throw dependency_unavailable{std::move(message)};
   case code::connect_timeout:
      throw connect_timeout{std::move(message)};
   case code::handshake_timeout:
      throw handshake_timeout{std::move(message)};
   case code::idle_timeout:
      throw idle_timeout{std::move(message)};
   case code::tls_failed:
      throw tls_failed{std::move(message)};
   case code::peer_verification_failed:
      throw peer_verification_failed{std::move(message)};
   case code::alpn_mismatch:
      throw alpn_mismatch{std::move(message)};
   case code::frame_too_large:
      throw frame_too_large{std::move(message)};
   case code::malformed_frame:
      throw malformed_frame{std::move(message)};
   case code::backpressure_rejected:
      throw backpressure_rejected{std::move(message)};
   case code::connection_closed:
      throw connection_closed{std::move(message)};
   case code::stream_closed:
      throw stream_closed{std::move(message)};
   case code::stream_reset:
      throw stream_reset{std::move(message)};
   case code::canceled:
      throw canceled{std::move(message)};
   case code::unsupported:
      throw unsupported{std::move(message)};
   case code::internal:
      throw internal{std::move(message)};
   }
   throw internal{std::move(message)};
}

} // namespace fcl::quic::exceptions
