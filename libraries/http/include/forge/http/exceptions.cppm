module;

#include <forge/exceptions/macros.hpp>

export module forge.http.exceptions;

export import forge.exceptions;

export namespace forge::http::exceptions {

enum class code : int {
   bad_request = 400,
   unauthorized = 401,
   forbidden = 403,
   not_found = 404,
   method_not_allowed = 405,
   not_acceptable = 406,
   conflict = 409,
   payload_too_large = 413,
   unsupported_media_type = 415,
   request_header_fields_too_large = 431,
   too_many_requests = 429,
   internal = 500,
   unavailable = 503,
   gateway_timeout = 504,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.http")

using bad_request = forge::exceptions::coded_exception<code, code::bad_request>;
using unauthorized = forge::exceptions::coded_exception<code, code::unauthorized>;
using forbidden = forge::exceptions::coded_exception<code, code::forbidden>;
using not_found = forge::exceptions::coded_exception<code, code::not_found>;
using method_not_allowed = forge::exceptions::coded_exception<code, code::method_not_allowed>;
using not_acceptable = forge::exceptions::coded_exception<code, code::not_acceptable>;
using conflict = forge::exceptions::coded_exception<code, code::conflict>;
using payload_too_large = forge::exceptions::coded_exception<code, code::payload_too_large>;
using unsupported_media_type = forge::exceptions::coded_exception<code, code::unsupported_media_type>;
using request_header_fields_too_large =
   forge::exceptions::coded_exception<code, code::request_header_fields_too_large>;
using too_many_requests = forge::exceptions::coded_exception<code, code::too_many_requests>;
using internal = forge::exceptions::coded_exception<code, code::internal>;
using unavailable = forge::exceptions::coded_exception<code, code::unavailable>;
using gateway_timeout = forge::exceptions::coded_exception<code, code::gateway_timeout>;

} // namespace forge::http::exceptions
