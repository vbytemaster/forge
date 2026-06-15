module;

#include <fcl/exceptions/macros.hpp>

export module fcl.http.exceptions;

export import fcl.exceptions;

export namespace fcl::http::exceptions {

enum class code : int {
   bad_request = 400,
   unauthorized = 401,
   forbidden = 403,
   not_found = 404,
   method_not_allowed = 405,
   conflict = 409,
   payload_too_large = 413,
   request_header_fields_too_large = 431,
   too_many_requests = 429,
   internal = 500,
   unavailable = 503,
   gateway_timeout = 504,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.http")

using bad_request = fcl::exceptions::coded_exception<code, code::bad_request>;
using unauthorized = fcl::exceptions::coded_exception<code, code::unauthorized>;
using forbidden = fcl::exceptions::coded_exception<code, code::forbidden>;
using not_found = fcl::exceptions::coded_exception<code, code::not_found>;
using method_not_allowed = fcl::exceptions::coded_exception<code, code::method_not_allowed>;
using conflict = fcl::exceptions::coded_exception<code, code::conflict>;
using payload_too_large = fcl::exceptions::coded_exception<code, code::payload_too_large>;
using request_header_fields_too_large =
   fcl::exceptions::coded_exception<code, code::request_header_fields_too_large>;
using too_many_requests = fcl::exceptions::coded_exception<code, code::too_many_requests>;
using internal = fcl::exceptions::coded_exception<code, code::internal>;
using unavailable = fcl::exceptions::coded_exception<code, code::unavailable>;
using gateway_timeout = fcl::exceptions::coded_exception<code, code::gateway_timeout>;

} // namespace fcl::http::exceptions
