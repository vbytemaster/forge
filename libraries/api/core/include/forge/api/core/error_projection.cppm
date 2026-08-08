module;

#include <string>

export module forge.api.core.error_projection;

export import forge.api.core.descriptor;
export import forge.api.core.exceptions;
export import forge.exceptions;

export namespace forge::api::core {

[[nodiscard]] error_payload project_error(const method_descriptor& method, const forge::exceptions::base& error);
[[nodiscard]] error_payload make_core_error_payload(exceptions::code code, std::string message);
[[nodiscard]] error_payload make_internal_error_payload(std::string safe_message = "internal error");
[[noreturn]] void raise_remote_error(const error_payload& payload, const method_descriptor* method = nullptr);

} // namespace forge::api::core
