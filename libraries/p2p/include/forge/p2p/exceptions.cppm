module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <forge/exceptions/macros.hpp>

export module forge.p2p.exceptions;

export import forge.exceptions;

export namespace forge::p2p::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   invalid_identity = 2,
   protocol_error = 3,
   codec_error = 4,
   unsupported_protocol = 5,
   duplicate_protocol = 6,
   peer_not_found = 7,
   peer_verification_failed = 8,
   relay_not_available = 9,
   relay_rejected = 10,
   backpressure_rejected = 11,
   timeout = 12,
   canceled = 13,
   closed = 14,
   internal = 15,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.p2p")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using invalid_identity = forge::exceptions::coded_exception<code, code::invalid_identity>;
using protocol_error = forge::exceptions::coded_exception<code, code::protocol_error>;
using codec_error = forge::exceptions::coded_exception<code, code::codec_error>;
using unsupported_protocol = forge::exceptions::coded_exception<code, code::unsupported_protocol>;
using duplicate_protocol = forge::exceptions::coded_exception<code, code::duplicate_protocol>;
using peer_not_found = forge::exceptions::coded_exception<code, code::peer_not_found>;
using peer_verification_failed = forge::exceptions::coded_exception<code, code::peer_verification_failed>;
using relay_not_available = forge::exceptions::coded_exception<code, code::relay_not_available>;
using relay_rejected = forge::exceptions::coded_exception<code, code::relay_rejected>;
using backpressure_rejected = forge::exceptions::coded_exception<code, code::backpressure_rejected>;
using timeout = forge::exceptions::coded_exception<code, code::timeout>;
using canceled = forge::exceptions::coded_exception<code, code::canceled>;
using closed = forge::exceptions::coded_exception<code, code::closed>;
using internal = forge::exceptions::coded_exception<code, code::internal>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& error) noexcept {
   const auto& value = error.code();
   if (!value || std::string_view{value.category().name()} != "forge.p2p") {
      return std::nullopt;
   }
   return static_cast<code>(value.value());
}

[[nodiscard]] inline bool is(const forge::exceptions::base& error, code value) noexcept {
   return error.code() == forge::exceptions::make_error_code(value);
}

} // namespace forge::p2p::exceptions
