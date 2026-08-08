module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.api.core.binding;

namespace forge::api::core {
namespace {

void fail_stream_endpoints(
   const std::shared_ptr<detail::stream_endpoint>& input,
   const std::shared_ptr<detail::stream_endpoint>& output) noexcept {
   const auto error = std::make_exception_ptr(
      exceptions::protocol_error{"API stream binding failed"});
   if (input) {
      input->fail(error);
   }
   if (output) {
      output->fail(error);
   }
}

[[nodiscard]] call_context make_context(const frame& value) {
   return call_context{
      .id = value.id,
      .api = value.api,
      .method = value.method,
      .meta = value.meta,
      .payload = value.payload,
      .codec = value.codec,
      .kind = value.kind,
   };
}

[[nodiscard]] frame make_api_not_exported_response(const frame& request) {
   auto response = frame{
      .kind = frame_kind::error,
      .id = request.id,
      .api = request.api,
      .method = request.method,
      .meta = request.meta,
      .codec = request.codec,
   };
   forge::raw::pack(
      response.payload,
      error_payload{
         .error = "api_not_exported",
         .message = "API is not exported by this binding plan",
         .retryable = false,
         .status_code = status::permission_denied,
         .identity = {
            .category = "forge.api",
            .code = static_cast<std::uint32_t>(
               exceptions::code::incompatible_version),
         },
      });
   return response;
}

[[nodiscard]] const descriptor*
find_export(const std::vector<descriptor>& exports,
            const api_ref& requested) noexcept {
   for (const auto& available : exports) {
      if (compatible(available, requested)) {
         return &available;
      }
   }
   return nullptr;
}

[[nodiscard]] bool exports_api(const binding_plan& plan,
                               api_ref requested) noexcept {
   return plan.exports.empty() ||
          find_export(plan.exports, requested) != nullptr;
}

[[nodiscard]] bool method_hidden_by_export(const binding_plan& plan,
                                           api_ref requested,
                                           std::string_view method) noexcept {
   const auto* exported =
      plan.exports.empty()
         ? (plan.local == nullptr ? nullptr : plan.local->describe(requested))
         : find_export(plan.exports, requested);
   if (exported == nullptr) {
      return false;
   }
   if (const auto* exported_method = find_method(*exported, method)) {
      return exported_method->since_revision > requested.min_revision;
   }
   if (plan.exports.empty() || plan.local == nullptr) {
      return false;
   }
   const auto* local_descriptor = plan.local->describe(std::move(requested));
   return local_descriptor != nullptr &&
          find_method(*local_descriptor, method) != nullptr;
}

void sort_interceptors(std::vector<interceptor_step>& interceptors) {
   std::sort(interceptors.begin(), interceptors.end(),
             [](const auto& left, const auto& right) {
                if (left.phase != right.phase) {
                   return static_cast<unsigned>(left.phase) <
                          static_cast<unsigned>(right.phase);
                }
                if (left.order != right.order) {
                   return left.order < right.order;
                }
                return left.id < right.id;
             });
}

void validate_interceptors(
   const std::vector<interceptor_step>& interceptors) {
   auto ids = std::set<std::string>{};
   for (const auto& step : interceptors) {
      if (step.id.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                               "API interceptor id must not be empty");
      }
      if (!step.handler) {
         FORGE_THROW_EXCEPTION(
            exceptions::protocol_error,
            "API interceptor handler must not be empty",
            forge::exceptions::ctx("interceptor", step.id));
      }
      if (!ids.insert(step.id).second) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                               "duplicate API interceptor id",
                               forge::exceptions::ctx("interceptor", step.id));
      }
   }
}

boost::asio::awaitable<void>
run_before_interceptors(const binding_plan& plan, frame& request) {
   auto context = make_context(request);
   for (const auto& step : plan.interceptors) {
      if (step.handler && step.phase <= interceptor_phase::before_call) {
         co_await step.handler(context);
      }
   }
   request.meta = std::move(context.meta);
   request.payload = std::move(context.payload);
}

boost::asio::awaitable<void>
run_terminal_interceptors(const binding_plan& plan, frame& response) {
   auto context = make_context(response);
   for (const auto& step : plan.interceptors) {
      const auto matches =
         response.kind == frame_kind::error
            ? step.phase == interceptor_phase::error
            : step.phase == interceptor_phase::after_call;
      if (step.handler && matches) {
         co_await step.handler(context);
      }
   }
   response.meta = std::move(context.meta);
   response.payload = std::move(context.payload);
}

} // namespace

interceptor_builder& interceptor_builder::id(std::string value) {
   value_.id = std::move(value);
   return *this;
}

interceptor_builder&
interceptor_builder::phase(interceptor_phase value) noexcept {
   value_.phase = value;
   return *this;
}

interceptor_builder& interceptor_builder::order(int value) noexcept {
   value_.order = value;
   return *this;
}

interceptor_builder&
interceptor_builder::handler(interceptor_handler value) {
   value_.handler = std::move(value);
   return *this;
}

interceptor_step interceptor_builder::build() {
   return std::move(value_);
}

interceptor_builder interceptor() {
   return interceptor_builder{};
}

boost::asio::awaitable<frame> binding_plan::dispatch(frame request) const {
   if (local == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   if (!exports_api(*this, request.api) ||
       method_hidden_by_export(*this, request.api, request.method)) {
      co_return make_api_not_exported_response(request);
   }

   co_await run_before_interceptors(*this, request);
   auto response = co_await local->dispatch(std::move(request));
   co_await run_terminal_interceptors(*this, response);
   co_return response;
}

boost::asio::awaitable<frame>
binding_plan::dispatch_stream(
   frame request, std::shared_ptr<detail::stream_endpoint> input,
   std::shared_ptr<detail::stream_endpoint> output) const {
   if (local == nullptr) {
      fail_stream_endpoints(input, output);
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   if (!exports_api(*this, request.api) ||
       method_hidden_by_export(*this, request.api, request.method)) {
      fail_stream_endpoints(input, output);
      co_return make_api_not_exported_response(request);
   }

   try {
      co_await run_before_interceptors(*this, request);
      auto response = co_await local->dispatch_stream(
         std::move(request), input, output);
      co_await run_terminal_interceptors(*this, response);
      if (response.kind == frame_kind::error) {
         fail_stream_endpoints(input, output);
      }
      co_return response;
   } catch (...) {
      fail_stream_endpoints(input, output);
      throw;
   }
}

binding_builder& binding_builder::serve(const registry& apis) {
   plan_.local = &apis;
   return *this;
}

binding_builder& binding_builder::serve(const view& apis) {
   plan_.local = &apis.registry_ref();
   return *this;
}

binding_builder&
binding_builder::interceptor(interceptor_step step) {
   plan_.interceptors.push_back(std::move(step));
   return *this;
}

binding_plan binding_builder::build() {
   sort_interceptors(plan_.interceptors);
   validate_interceptors(plan_.interceptors);
   return std::move(plan_);
}

binding_builder binding() {
   return {};
}

} // namespace forge::api::core
