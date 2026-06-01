module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.websocket.exceptions;

export import fcl.exceptions;

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

using invalid_handshake = fcl::exceptions::coded_exception<code, code::invalid_handshake>;
using frame_too_large = fcl::exceptions::coded_exception<code, code::frame_too_large>;
using malformed_frame = fcl::exceptions::coded_exception<code, code::malformed_frame>;
using backpressure_rejected = fcl::exceptions::coded_exception<code, code::backpressure_rejected>;
using closed = fcl::exceptions::coded_exception<code, code::closed>;
using timeout = fcl::exceptions::coded_exception<code, code::timeout>;
using internal = fcl::exceptions::coded_exception<code, code::internal>;

} // namespace fcl::websocket::exceptions
