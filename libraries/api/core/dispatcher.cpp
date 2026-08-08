module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string_view>
#include <utility>

module forge.api.core.dispatcher;

namespace forge::api::core {
namespace {

[[nodiscard]] bool reserved_metadata_key(std::string_view key) noexcept {
   return key.starts_with(trusted_metadata_prefix);
}

void apply_remote_metadata_boundary(frame& value,
                                    const metadata& trusted) {
   auto merged = metadata{};
   merged.reserve(value.meta.size() + trusted.size());
   for (auto& entry : value.meta) {
      if (!reserved_metadata_key(entry.key)) {
         merged.push_back(std::move(entry));
      }
   }
   for (const auto& entry : trusted) {
      merged.push_back(entry);
   }
   value.meta = std::move(merged);
}

void validate_request(const frame& value, const dispatch_options& options) {
   if (value.kind != frame_kind::request) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "API dispatcher accepts only request frames");
   }
   if (value.id.value == 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "API call_id zero is reserved for session control");
   }
   if (value.codec != options.codec) {
      FORGE_THROW_EXCEPTION(exceptions::codec_failed,
                            "API frame codec is not accepted",
                            forge::exceptions::ctx("codec",
                                                   value.codec.value));
   }
}

} // namespace

struct frame_dispatcher::impl {
   binding_plan plan;
   dispatch_options options;

   impl(binding_plan plan_value, dispatch_options options_value)
       : plan{std::move(plan_value)}, options{std::move(options_value)} {}
};

frame_dispatcher::frame_dispatcher(binding_plan plan, dispatch_options options)
    : impl_{std::make_shared<impl>(std::move(plan), std::move(options))} {}

frame_dispatcher::~frame_dispatcher() = default;
frame_dispatcher::frame_dispatcher(frame_dispatcher&&) noexcept = default;
frame_dispatcher&
frame_dispatcher::operator=(frame_dispatcher&&) noexcept = default;

boost::asio::awaitable<frame> frame_dispatcher::dispatch(frame value) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "invalid API frame dispatcher");
   }
   validate_request(value, impl_->options);
   apply_remote_metadata_boundary(value, impl_->options.trusted_metadata);
   co_return co_await impl_->plan.dispatch(std::move(value));
}

boost::asio::awaitable<frame>
frame_dispatcher::dispatch_stream(
   frame value, std::shared_ptr<detail::stream_endpoint> input,
   std::shared_ptr<detail::stream_endpoint> output) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "invalid API frame dispatcher");
   }
   validate_request(value, impl_->options);
   apply_remote_metadata_boundary(value, impl_->options.trusted_metadata);
   co_return co_await impl_->plan.dispatch_stream(
      std::move(value), std::move(input), std::move(output));
}

const dispatch_options& frame_dispatcher::options() const noexcept {
   return impl_->options;
}

} // namespace forge::api::core
