module;

#include <boost/describe.hpp>
#include <cstdint>
#include <fcl/exception/macros.hpp>

export module fcl.api.exceptions;

export import fcl.exception.exception;

export namespace fcl::api::exceptions {

enum class code : std::uint16_t {
   method_not_found = 1,
   incompatible_version = 2,
   codec_failed = 3,
   deadline_exceeded = 4,
   cancelled = 5,
   remote_internal = 6,
   protocol_error = 7,
   resource_exhausted = 8,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.api")

using method_not_found = fcl::exception::coded_exception<code, code::method_not_found>;
using incompatible_version = fcl::exception::coded_exception<code, code::incompatible_version>;
using codec_failed = fcl::exception::coded_exception<code, code::codec_failed>;
using deadline_exceeded = fcl::exception::coded_exception<code, code::deadline_exceeded>;
using cancelled = fcl::exception::coded_exception<code, code::cancelled>;
using remote_internal = fcl::exception::coded_exception<code, code::remote_internal>;
using protocol_error = fcl::exception::coded_exception<code, code::protocol_error>;
using resource_exhausted = fcl::exception::coded_exception<code, code::resource_exhausted>;

BOOST_DESCRIBE_ENUM(code, method_not_found, incompatible_version, codec_failed, deadline_exceeded, cancelled,
                    remote_internal, protocol_error, resource_exhausted)

} // namespace fcl::api::exceptions
