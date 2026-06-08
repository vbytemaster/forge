module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.api.transport.connection;

namespace fcl::api::transport {

struct connection::impl {
   class stream_invoker final : public fcl::api::remote_invoker {
    public:
      stream_invoker(std::shared_ptr<impl> owner, fcl::api::descriptor contract)
          : owner_(std::move(owner)), contract_(std::move(contract)) {}

      boost::asio::awaitable<fcl::api::response> async_call(fcl::api::request value) override;

    private:
      std::shared_ptr<impl> owner_;
      fcl::api::descriptor contract_;
   };

   client transport;
};

connection::connection() = default;

connection::connection(fcl::transport::stream stream, options value) : impl_(std::make_shared<impl>()) {
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

boost::asio::awaitable<std::shared_ptr<fcl::api::remote_invoker>>
connection::open_remote_invoker(fcl::api::api_ref, fcl::api::descriptor remote_descriptor) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::cancelled, "API transport connection is closed");
   }
   co_return std::make_shared<impl::stream_invoker>(impl_, std::move(remote_descriptor));
}

boost::asio::awaitable<fcl::api::response> connection::impl::stream_invoker::async_call(fcl::api::request value) {
   auto outbound = fcl::api::frame{
       .kind = fcl::api::frame_kind::request,
       .api = std::move(value.api),
       .method = std::move(value.method),
       .meta = std::move(value.meta),
       .codec = owner_->transport.settings().codec,
       .payload = std::move(value.body),
   };

   auto inbound = co_await owner_->transport.async_call(std::move(outbound));
   if (inbound.kind == fcl::api::frame_kind::error) {
      co_return fcl::api::response{
          .api = std::move(inbound.api),
          .method = std::move(inbound.method),
          .meta = std::move(inbound.meta),
          .codec = std::move(inbound.codec),
          .error = fcl::api::unpack_body<fcl::api::error_payload>(inbound.payload),
      };
   }
   if (inbound.kind != fcl::api::frame_kind::response) {
      FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error,
                          "API transport connection received non-response frame",
                          fcl::exceptions::ctx("method", inbound.method));
   }
   co_return fcl::api::response{
       .api = std::move(inbound.api),
       .method = std::move(inbound.method),
       .meta = std::move(inbound.meta),
       .codec = std::move(inbound.codec),
       .body = std::move(inbound.payload),
   };
}

} // namespace fcl::api::transport
