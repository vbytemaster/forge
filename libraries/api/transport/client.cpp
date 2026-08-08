module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <memory>
#include <optional>
#include <utility>

module forge.api.transport.client;

namespace forge::api::transport {
namespace {

[[nodiscard]] forge::api::core::frame make_frame(
   forge::api::core::request value) {
   return forge::api::core::frame{
      .kind = forge::api::core::frame_kind::request,
      .api = std::move(value.api),
      .method = std::move(value.method),
      .meta = std::move(value.meta),
      .codec = std::move(value.codec),
      .payload = std::move(value.body),
   };
}

[[nodiscard]] forge::api::core::response make_response(
   forge::api::core::frame value) {
   if (value.kind == forge::api::core::frame_kind::cancel) {
      FORGE_THROW_EXCEPTION(exceptions::cancelled,
                            "API transport call was cancelled by the peer",
                            forge::exceptions::ctx("call_id", value.id.value));
   }
   if (value.kind != forge::api::core::frame_kind::response &&
       value.kind != forge::api::core::frame_kind::error) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "API transport received a non-terminal frame");
   }
   auto result = forge::api::core::response{
      .api = std::move(value.api),
      .method = std::move(value.method),
      .meta = std::move(value.meta),
      .codec = std::move(value.codec),
   };
   if (value.kind == forge::api::core::frame_kind::error) {
      result.error =
         forge::api::core::unpack_body<forge::api::core::error_payload>(
            value.payload);
   } else {
      result.body = std::move(value.payload);
   }
   return result;
}

} // namespace

client::client() = default;

client::client(forge::net::transport::stream stream, options value)
    : session_{std::make_shared<forge::api::stream::session>(
         std::move(stream), std::move(value))} {}

client::client(std::shared_ptr<forge::api::stream::session> session,
               forge::api::core::descriptor descriptor)
    : session_{std::move(session)}, descriptor_{std::move(descriptor)} {}

client::~client() = default;
client::client(client&&) noexcept = default;
client& client::operator=(client&&) noexcept = default;

bool client::valid() const noexcept {
   return session_ && session_->valid();
}

const options& client::settings() const noexcept {
   static const auto defaults = options{};
   return session_ ? session_->settings() : defaults;
}

boost::asio::awaitable<forge::api::core::response>
client::async_call(forge::api::core::request value) {
   return async_call_impl(session_, descriptor_, std::move(value));
}

boost::asio::awaitable<forge::api::core::response>
client::async_call_impl(
   std::shared_ptr<forge::api::stream::session> session,
   std::optional<forge::api::core::descriptor> descriptor,
   forge::api::core::request value) {
   if (!session) {
      FORGE_THROW_EXCEPTION(exceptions::cancelled,
                            "API transport client is closed");
   }
   const auto* method = descriptor
                           ? forge::api::core::find_method(*descriptor,
                                                          value.method)
                           : nullptr;
   co_return make_response(co_await session->async_call(
      make_frame(std::move(value)), {},
      method ? std::optional<forge::api::core::method_descriptor>{*method}
             : std::nullopt));
}

boost::asio::awaitable<forge::api::core::response>
client::async_stream_call(
   forge::api::core::request value, forge::api::core::method_kind kind,
   std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
   std::shared_ptr<forge::api::core::detail::stream_endpoint> output) {
   return async_stream_call_impl(
      session_, descriptor_, std::move(value), kind, std::move(input),
      std::move(output));
}

boost::asio::awaitable<forge::api::core::response>
client::async_stream_call_impl(
   std::shared_ptr<forge::api::stream::session> session,
   std::optional<forge::api::core::descriptor> descriptor,
   forge::api::core::request value, forge::api::core::method_kind kind,
   std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
   std::shared_ptr<forge::api::core::detail::stream_endpoint> output) {
   if (!session) {
      FORGE_THROW_EXCEPTION(exceptions::cancelled,
                            "API transport client is closed");
   }
   const auto* method = descriptor
                           ? forge::api::core::find_method(*descriptor,
                                                          value.method)
                           : nullptr;
   co_return make_response(co_await session->async_stream_call(
      make_frame(std::move(value)), kind, std::move(input),
      std::move(output), {},
      method ? std::optional<forge::api::core::method_descriptor>{*method}
             : std::nullopt));
}

boost::asio::awaitable<void> client::async_close() {
   return async_close_impl(session_);
}

boost::asio::awaitable<void>
client::async_close_impl(
   std::shared_ptr<forge::api::stream::session> session) {
   if (session) {
      co_await session->async_close();
   }
}

void client::cancel() noexcept {
   if (session_) {
      session_->cancel();
   }
}

} // namespace forge::api::transport
