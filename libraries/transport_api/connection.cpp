module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.transport.api.connection;

namespace forge::transport::api {

struct connection::impl {
   class stream_invoker final : public forge::api::remote_invoker {
    public:
      stream_invoker(std::shared_ptr<impl> owner, forge::api::descriptor contract)
          : owner_(std::move(owner)), contract_(std::move(contract)) {}

      boost::asio::awaitable<forge::api::response> async_call(forge::api::request value) override;

    private:
      std::shared_ptr<impl> owner_;
      forge::api::descriptor contract_;
   };

   client transport;
};

connection::connection() = default;

connection::connection(forge::transport::stream stream, options value) : impl_(std::make_shared<impl>()) {
   impl_->transport = client{std::move(stream), std::move(value)};
}

connection::~connection() = default;
connection::connection(connection&&) noexcept = default;
connection& connection::operator=(connection&&) noexcept = default;

bool connection::valid() const noexcept {
   return impl_ && impl_->transport.valid();
}

const options& connection::settings() const noexcept {
   static const auto defaults = options{};
   return impl_ ? impl_->transport.settings() : defaults;
}

boost::asio::awaitable<void> connection::async_close() {
   if (!impl_) {
      co_return;
   }
   co_await impl_->transport.async_close();
}

void connection::cancel() noexcept {
   if (impl_) {
      impl_->transport.cancel();
   }
}

boost::asio::awaitable<std::shared_ptr<forge::api::remote_invoker>>
connection::open_remote_invoker(forge::api::api_ref, forge::api::descriptor remote_descriptor) {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::cancelled, "API transport connection is closed");
   }
   co_return std::make_shared<impl::stream_invoker>(impl_, std::move(remote_descriptor));
}

boost::asio::awaitable<forge::api::response> connection::impl::stream_invoker::async_call(forge::api::request value) {
   auto outbound = forge::api::frame{
       .kind = forge::api::frame_kind::request,
       .api = std::move(value.api),
       .method = std::move(value.method),
       .meta = std::move(value.meta),
       .codec = owner_->transport.settings().codec,
       .payload = std::move(value.body),
   };

   auto inbound = co_await owner_->transport.async_call(std::move(outbound));
   if (inbound.kind == forge::api::frame_kind::error) {
      co_return forge::api::response{
          .api = std::move(inbound.api),
          .method = std::move(inbound.method),
          .meta = std::move(inbound.meta),
          .codec = std::move(inbound.codec),
          .error = forge::api::unpack_body<forge::api::error_payload>(inbound.payload),
      };
   }
   if (inbound.kind != forge::api::frame_kind::response) {
      FORGE_THROW_EXCEPTION(forge::api::exceptions::protocol_error,
                          "API transport connection received non-response frame",
                          forge::exceptions::ctx("method", inbound.method));
   }
   co_return forge::api::response{
       .api = std::move(inbound.api),
       .method = std::move(inbound.method),
       .meta = std::move(inbound.meta),
       .codec = std::move(inbound.codec),
       .body = std::move(inbound.payload),
   };
}

} // namespace forge::transport::api
