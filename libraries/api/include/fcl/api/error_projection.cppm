module;

#include <string>

export module fcl.api.error_projection;

export import fcl.api.descriptor;
export import fcl.api.exceptions;
export import fcl.exceptions;

export namespace fcl::api {

[[nodiscard]] error_payload project_error(const method_descriptor& method, const fcl::exceptions::base& error);
[[nodiscard]] error_payload make_internal_error_payload(std::string safe_message = "internal error");
[[noreturn]] void raise_remote_error(const error_payload& payload, const method_descriptor* method = nullptr);

} // namespace fcl::api
