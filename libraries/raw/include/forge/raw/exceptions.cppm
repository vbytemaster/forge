module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.raw.exceptions;

export import forge.exceptions;

export namespace forge::raw::exceptions {

enum class code : std::uint16_t {
   range_error = 1,
   codec_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.raw")

using range_error = forge::exceptions::coded_exception<code, code::range_error>;
using codec_error = forge::exceptions::coded_exception<code, code::codec_error>;

} // namespace forge::raw::exceptions
