module;

#include <cstdint>
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

} // namespace fcl::quic::exceptions
