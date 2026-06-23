module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.websocket.exceptions;

export import forge.exceptions;

export namespace forge::websocket::exceptions {

enum class code : std::uint16_t {
   invalid_handshake = 1,
   frame_too_large = 2,
   malformed_frame = 3,
   backpressure_rejected = 4,
   closed = 5,
   timeout = 6,
   internal = 7,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.websocket")

using invalid_handshake = forge::exceptions::coded_exception<code, code::invalid_handshake>;
using frame_too_large = forge::exceptions::coded_exception<code, code::frame_too_large>;
using malformed_frame = forge::exceptions::coded_exception<code, code::malformed_frame>;
using backpressure_rejected = forge::exceptions::coded_exception<code, code::backpressure_rejected>;
using closed = forge::exceptions::coded_exception<code, code::closed>;
using timeout = forge::exceptions::coded_exception<code, code::timeout>;
using internal = forge::exceptions::coded_exception<code, code::internal>;

} // namespace forge::websocket::exceptions
