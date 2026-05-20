module;

#include <cstdint>
#include <fcl/exception/macros.hpp>

export module fcl.websocket.exceptions;

export import fcl.exception.exception;

export namespace fcl::websocket::exceptions {

enum class code : std::uint16_t {
   invalid_handshake = 1,
   frame_too_large = 2,
   malformed_frame = 3,
   backpressure_rejected = 4,
   closed = 5,
   timeout = 6,
   internal = 7,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.websocket")

using invalid_handshake = fcl::exception::coded_exception<code, code::invalid_handshake>;
using frame_too_large = fcl::exception::coded_exception<code, code::frame_too_large>;
using malformed_frame = fcl::exception::coded_exception<code, code::malformed_frame>;
using backpressure_rejected = fcl::exception::coded_exception<code, code::backpressure_rejected>;
using closed = fcl::exception::coded_exception<code, code::closed>;
using timeout = fcl::exception::coded_exception<code, code::timeout>;
using internal = fcl::exception::coded_exception<code, code::internal>;

} // namespace fcl::websocket::exceptions
