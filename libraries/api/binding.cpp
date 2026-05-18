module;

#include <fcl/exception/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <set>
#include <string>
#include <utility>

module fcl.api.binding;

namespace fcl::api {

namespace {

[[nodiscard]] bool terminal(frame_kind kind) noexcept {
   return kind == frame_kind::response || kind == frame_kind::error || kind == frame_kind::stream_end ||
          kind == frame_kind::cancel;
}

[[nodiscard]] call_context make_context(const frame& value) {
   return call_context{
       .id = value.id,
       .api = value.api,
       .method = value.method,
       .meta = value.meta,
       .codec = value.codec,
       .kind = value.kind,
   };
}

[[nodiscard]] frame make_error_response(const frame& request, error_payload payload) {
   auto response = frame{
       .kind = frame_kind::error,
       .id = request.id,
       .api = request.api,
       .method = request.method,
       .meta = request.meta,
       .codec = request.codec,
   };
   fcl::raw::pack(response.payload, payload);
   return response;
}

[[nodiscard]] bool exported(const std::vector<descriptor>& exports, const api_ref& requested) noexcept {
   if (exports.empty()) {
      return true;
   }
   return std::any_of(exports.begin(), exports.end(), [&](const auto& available) {
      return compatible(available, requested);
   });
}

void sort_interceptors(std::vector<interceptor_step>& interceptors) {
   std::sort(interceptors.begin(), interceptors.end(), [](const auto& left, const auto& right) {
      if (left.phase != right.phase) {
         return static_cast<unsigned>(left.phase) < static_cast<unsigned>(right.phase);
      }
      if (left.order != right.order) {
         return left.order < right.order;
      }
      return left.id < right.id;
   });
}

void validate_interceptors(const std::vector<interceptor_step>& interceptors) {
   auto ids = std::set<std::string>{};
   for (const auto& step : interceptors) {
      if (step.id.empty()) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "API interceptor id must not be empty");
      }
      if (!step.handler) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "API interceptor handler must not be empty",
                             fcl::exception::ctx("interceptor", step.id));
      }
      if (!ids.insert(step.id).second) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "duplicate API interceptor id",
                             fcl::exception::ctx("interceptor", step.id));
      }
   }
}

} // namespace

interceptor_builder& interceptor_builder::id(std::string value) {
   value_.id = std::move(value);
   return *this;
}

interceptor_builder& interceptor_builder::phase(interceptor_phase value) noexcept {
   value_.phase = value;
   return *this;
}

interceptor_builder& interceptor_builder::order(int value) noexcept {
   value_.order = value;
   return *this;
}

interceptor_builder& interceptor_builder::handler(interceptor_handler value) {
   value_.handler = std::move(value);
   return *this;
}

interceptor_step interceptor_builder::build() {
   return std::move(value_);
}

interceptor_builder interceptor() {
   return interceptor_builder{};
}

call_runtime::call_runtime(call_runtime_options options) : options_{options} {}

void call_runtime::observe(const frame& value) {
   const auto id = value.id.value;
   if (id == 0) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "API frame call_id must not be zero");
   }

   if (value.kind == frame_kind::request) {
      if (active_.contains(id)) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "duplicate active API call_id",
                             fcl::exception::ctx("call_id", id));
      }
      if (active_.size() >= options_.max_inflight) {
         FCL_THROW_EXCEPTION(exceptions::resource_exhausted, "API max inflight calls exceeded",
                             fcl::exception::ctx("max_inflight", options_.max_inflight));
      }
      active_.emplace(id, active_call{.started_at = std::chrono::steady_clock::now()});
      return;
   }

   const auto active = active_.find(id);
   if (active == active_.end()) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "API frame references unknown call_id",
                          fcl::exception::ctx("call_id", id));
   }

   if (options_.deadline.count() > 0 &&
       std::chrono::steady_clock::now() - active->second.started_at > options_.deadline) {
      active_.erase(active);
      FCL_THROW_EXCEPTION(exceptions::deadline_exceeded, "API call deadline exceeded",
                          fcl::exception::ctx("call_id", id));
   }

   if (terminal(value.kind)) {
      active_.erase(id);
   }
}

std::size_t call_runtime::active_calls() const noexcept {
   return active_.size();
}

boost::asio::awaitable<frame> binding_plan::dispatch(frame request) const {
   auto calls = call_runtime{};
   co_return co_await dispatch(std::move(request), calls);
}

boost::asio::awaitable<frame> binding_plan::dispatch(frame request, call_runtime& calls) const {
   auto responses = co_await dispatch_many(std::move(request), calls);
   co_return std::move(responses.front());
}

boost::asio::awaitable<std::vector<frame>> binding_plan::dispatch_many(frame request) const {
   auto calls = call_runtime{};
   co_return co_await dispatch_many(std::move(request), calls);
}

boost::asio::awaitable<std::vector<frame>> binding_plan::dispatch_many(frame request, call_runtime& calls) const {
   if (local == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::incompatible_version, "API binding plan has no local registry");
   }
   if (!exported(exports, request.api)) {
      co_return std::vector<frame>{make_error_response(
          request, error_payload{
                       .error = "api_not_exported",
                       .message = "API is not exported by this binding plan",
                       .retryable = false,
                       .status_code = status::permission_denied,
                       .identity =
                           {
                               .category = "fcl.api",
                               .code = static_cast<std::uint32_t>(exceptions::code::incompatible_version),
                           },
                   })};
   }

   calls.observe(request);

   auto context = make_context(request);
   for (const auto& step : interceptors) {
      if (step.handler && step.phase <= interceptor_phase::before_call) {
         co_await step.handler(context);
      }
   }

   auto responses = co_await local->dispatch_many(std::move(request));

   for (auto& response : responses) {
      auto response_context = make_context(response);
      for (const auto& step : interceptors) {
         if (step.handler &&
             ((response.kind == frame_kind::error && step.phase == interceptor_phase::error) ||
              (response.kind != frame_kind::error && step.phase == interceptor_phase::after_call))) {
            co_await step.handler(response_context);
         }
      }
      calls.observe(response);
   }

   co_return responses;
}

boost::asio::awaitable<std::vector<frame>> binding_plan::dispatch_stream(std::vector<frame> frames) const {
   if (local == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::incompatible_version, "API binding plan has no local registry");
   }
   if (frames.empty()) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "API stream dispatch requires at least one frame");
   }
   if (!exported(exports, frames.front().api)) {
      co_return std::vector<frame>{make_error_response(
          frames.front(), error_payload{
                            .error = "api_not_exported",
                            .message = "API is not exported by this binding plan",
                            .retryable = false,
                            .status_code = status::permission_denied,
                            .identity =
                                {
                                    .category = "fcl.api",
                                    .code = static_cast<std::uint32_t>(exceptions::code::incompatible_version),
                                },
                         })};
   }

   auto context = make_context(frames.front());
   for (const auto& step : interceptors) {
      if (step.handler && step.phase <= interceptor_phase::before_call) {
         co_await step.handler(context);
      }
   }

   auto responses = co_await local->dispatch_stream(std::move(frames));

   for (auto& response : responses) {
      auto response_context = make_context(response);
      for (const auto& step : interceptors) {
         if (step.handler &&
             ((response.kind == frame_kind::error && step.phase == interceptor_phase::error) ||
              (response.kind != frame_kind::error && step.phase == interceptor_phase::after_call))) {
            co_await step.handler(response_context);
         }
      }
   }

   co_return responses;
}

binding_builder& binding_builder::serve(const registry& apis) {
   plan_.local = &apis;
   return *this;
}

binding_builder& binding_builder::serve(const view& apis) {
   plan_.local = &apis.registry_ref();
   return *this;
}

binding_builder& binding_builder::interceptor(interceptor_step step) {
   plan_.interceptors.push_back(std::move(step));
   return *this;
}

binding_plan binding_builder::build() {
   sort_interceptors(plan_.interceptors);
   validate_interceptors(plan_.interceptors);
   return std::move(plan_);
}

binding_builder binding() {
   return binding_builder{};
}

} // namespace fcl::api
