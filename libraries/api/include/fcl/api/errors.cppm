module;

#include <string>

export module fcl.api.errors;

export import fcl.api.descriptor;
export import fcl.api.exceptions;
export import fcl.exception.exception;

export namespace fcl::api {

[[nodiscard]] const error_descriptor* find_error(const method_descriptor& method,
                                                 const fcl::exception::base& error) noexcept;
[[nodiscard]] error_payload make_error_payload(const fcl::exception::base& error,
                                               const error_descriptor* descriptor = nullptr);
[[nodiscard]] error_payload make_internal_error_payload(std::string safe_message = "internal error");
[[noreturn]] void throw_remote_error(const error_payload& payload, const method_descriptor* method = nullptr);

} // namespace fcl::api
