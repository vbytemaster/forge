module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.api.dispatcher;

namespace fcl::api {
namespace {

[[nodiscard]] const descriptor* find_export(const std::vector<descriptor>& exports,
                                            const api_ref& requested) noexcept {
   for (const auto& available : exports) {
      if (compatible(available, requested)) {
         return &available;
      }
   }
   return nullptr;
}

[[nodiscard]] const method_descriptor* exported_method(const binding_plan& plan, api_ref requested,
                                                       std::string_view method_name) noexcept {
   const auto* descriptor = plan.exports.empty() ? (plan.local == nullptr ? nullptr : plan.local->describe(requested))
                                                 : find_export(plan.exports, requested);
   return descriptor == nullptr ? nullptr : find_method(*descriptor, method_name);
}

[[nodiscard]] bool grouped_stream_method(const binding_plan& plan, const frame& request) noexcept {
   const auto* method = exported_method(plan, request.api, request.method);
   return method != nullptr &&
          (method->kind == method_kind::client_stream || method->kind == method_kind::bidirectional_stream);
}

} // namespace

struct frame_dispatcher::impl {
   binding_plan plan;
   dispatch_options options;
   call_runtime calls;
   std::unordered_map<std::uint64_t, std::vector<frame>> grouped;

   impl(binding_plan plan_value, dispatch_options options_value)
       : plan(std::move(plan_value)),
         options(std::move(options_value)),
         calls(call_runtime_options{.max_inflight = options.max_inflight, .deadline = options.deadline}) {}
};

frame_dispatcher::frame_dispatcher(binding_plan plan, dispatch_options options)
    : impl_(std::make_shared<impl>(std::move(plan), std::move(options))) {}

frame_dispatcher::~frame_dispatcher() = default;
frame_dispatcher::frame_dispatcher(frame_dispatcher&&) noexcept = default;
frame_dispatcher& frame_dispatcher::operator=(frame_dispatcher&&) noexcept = default;

boost::asio::awaitable<std::vector<frame>> frame_dispatcher::dispatch(frame value) {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "invalid API frame dispatcher");
   }
   if (value.codec != impl_->options.codec) {
      FCL_THROW_EXCEPTION(exceptions::codec_failed, "API frame codec is not accepted",
                          fcl::exceptions::ctx("codec", value.codec.value));
   }

   if (value.kind == frame_kind::request && grouped_stream_method(impl_->plan, value)) {
      impl_->calls.observe(value);
      if (impl_->grouped.size() >= impl_->options.max_inflight) {
         FCL_THROW_EXCEPTION(exceptions::resource_exhausted, "API grouped stream limit exceeded",
                             fcl::exceptions::ctx("max_inflight", impl_->options.max_inflight));
      }
      const auto id = value.id.value;
      if (!impl_->grouped.emplace(id, std::vector<frame>{std::move(value)}).second) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "duplicate active API stream",
                             fcl::exceptions::ctx("call_id", id));
      }
      co_return std::vector<frame>{};
   }

   if (auto active = impl_->grouped.find(value.id.value); active != impl_->grouped.end()) {
      if (value.kind == frame_kind::cancel) {
         impl_->calls.observe(value);
         impl_->grouped.erase(active);
         co_return std::vector<frame>{};
      }
      if (value.kind == frame_kind::stream_end) {
         try {
            impl_->calls.observe_input_stream_end(value);
         } catch (...) {
            impl_->grouped.erase(active);
            throw;
         }
      } else {
         impl_->calls.observe(value);
      }
      active->second.push_back(std::move(value));
      if (active->second.back().kind != frame_kind::stream_end) {
         co_return std::vector<frame>{};
      }
      auto frames = std::move(active->second);
      impl_->grouped.erase(active);
      co_return co_await impl_->plan.dispatch_stream(std::move(frames), impl_->calls);
   }

   co_return co_await impl_->plan.dispatch_many(std::move(value), impl_->calls);
}

const dispatch_options& frame_dispatcher::options() const noexcept {
   return impl_->options;
}

std::size_t frame_dispatcher::active_calls() const noexcept {
   return impl_ ? impl_->calls.active_calls() : 0;
}

std::size_t frame_dispatcher::grouped_calls() const noexcept {
   return impl_ ? impl_->grouped.size() : 0;
}

} // namespace fcl::api
