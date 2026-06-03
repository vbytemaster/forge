module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fcl/exceptions/macros.hpp>

export module fcl.quic.exceptions;

export import fcl.exceptions;

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

using invalid_endpoint = fcl::exceptions::coded_exception<code, code::invalid_endpoint>;
using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;
using dependency_unavailable = fcl::exceptions::coded_exception<code, code::dependency_unavailable>;
using connect_timeout = fcl::exceptions::coded_exception<code, code::connect_timeout>;
using handshake_timeout = fcl::exceptions::coded_exception<code, code::handshake_timeout>;
using idle_timeout = fcl::exceptions::coded_exception<code, code::idle_timeout>;
using tls_failed = fcl::exceptions::coded_exception<code, code::tls_failed>;
using peer_verification_failed = fcl::exceptions::coded_exception<code, code::peer_verification_failed>;
using alpn_mismatch = fcl::exceptions::coded_exception<code, code::alpn_mismatch>;
using frame_too_large = fcl::exceptions::coded_exception<code, code::frame_too_large>;
using malformed_frame = fcl::exceptions::coded_exception<code, code::malformed_frame>;
using backpressure_rejected = fcl::exceptions::coded_exception<code, code::backpressure_rejected>;
using connection_closed = fcl::exceptions::coded_exception<code, code::connection_closed>;
using stream_closed = fcl::exceptions::coded_exception<code, code::stream_closed>;
using stream_reset = fcl::exceptions::coded_exception<code, code::stream_reset>;
using canceled = fcl::exceptions::coded_exception<code, code::canceled>;
using unsupported = fcl::exceptions::coded_exception<code, code::unsupported>;
using internal = fcl::exceptions::coded_exception<code, code::internal>;

[[nodiscard]] inline std::optional<code> code_of(const fcl::exceptions::base& error) noexcept {
   const auto& value = error.code();
   if (!value || std::string_view{value.category().name()} != "fcl.quic") {
      return std::nullopt;
   }
   return static_cast<code>(value.value());
}

[[nodiscard]] inline bool is(const fcl::exceptions::base& error, code value) noexcept {
   return error.code() == fcl::exceptions::make_error_code(value);
}

} // namespace fcl::quic::exceptions
