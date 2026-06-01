module;

#include <string>
#include <utility>

module fcl.api.error_projection;

namespace fcl::api {

namespace {

std::string external_message(const fcl::exceptions::base& error) {
   if (error.message().empty()) {
      return "request failed";
   }
   return error.message();
}

const error_descriptor* find_error(const method_descriptor& method, const fcl::exceptions::base& error) noexcept {
   const auto& code = error.code();
   for (const auto& descriptor : method.errors) {
      if (descriptor.identity.category == code.category().name() &&
          descriptor.identity.code == static_cast<std::uint32_t>(code.value())) {
         return &descriptor;
      }
   }
   return nullptr;
}

error_payload make_error_payload(const fcl::exceptions::base& error, const error_descriptor* descriptor) {
   if (descriptor == nullptr) {
      return make_internal_error_payload();
   }

   const auto& code = error.code();
   return error_payload{
       .error = descriptor->name,
       .message = external_message(error),
       .retryable = descriptor->retryable,
       .status_code = descriptor->status_code,
       .identity =
           {
               .category = code.category().name(),
               .code = static_cast<std::uint32_t>(code.value()),
       },
   };
}

} // namespace

error_payload project_error(const method_descriptor& method, const fcl::exceptions::base& error) {
   return make_error_payload(error, find_error(method, error));
}

error_payload make_internal_error_payload(std::string safe_message) {
   return error_payload{
       .error = "internal",
       .message = std::move(safe_message),
       .retryable = false,
       .identity =
           {
               .category = "fcl.api",
               .code = static_cast<std::uint32_t>(exceptions::code::remote_internal),
           },
   };
}

void raise_remote_error(const error_payload& payload, const method_descriptor* method) {
   if (method != nullptr) {
      for (const auto& descriptor : method->errors) {
         if (descriptor.identity == payload.identity && descriptor.thrower) {
            descriptor.thrower(payload);
         }
      }
   }

   throw exceptions::remote_internal{
       payload.message.empty() ? std::string{"remote API error"} : payload.message,
       fcl::exceptions::make_fields(fcl::exceptions::ctx("remote.error", payload.error),
                                   fcl::exceptions::ctx("remote.category", payload.identity.category),
                                   fcl::exceptions::ctx("remote.code", payload.identity.code))};
}

} // namespace fcl::api
