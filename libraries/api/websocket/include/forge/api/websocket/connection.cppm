module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module forge.api.websocket.connection;

export import forge.api.core.connection;
export import forge.api.stream.options;
export import forge.net.websocket.connection;
import forge.api.stream.session;

export namespace forge::api::websocket {

class connection final : public forge::api::core::connection {
 public:
   connection();
   connection(forge::net::websocket::connection::ptr connection,
              forge::api::stream::options options = {});
   ~connection() override;

   connection(connection&&) noexcept;
   connection& operator=(connection&&) noexcept;

   connection(const connection&) = delete;
   connection& operator=(const connection&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const forge::api::stream::options& settings() const noexcept;

   boost::asio::awaitable<void> async_close() override;
   void cancel() noexcept override;

 private:
   static boost::asio::awaitable<void>
   async_close_impl(std::shared_ptr<forge::api::stream::session> session);
   static boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
   open_remote_invoker_impl(
      std::shared_ptr<forge::api::stream::session> session,
      forge::net::websocket::connection::ptr connection,
      forge::api::core::descriptor remote_descriptor,
      forge::api::core::capability_set capabilities);

   boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
   open_remote_invoker(forge::api::core::api_ref requested,
                       forge::api::core::descriptor remote_descriptor) override;

   forge::api::core::capability_set capabilities_;
   forge::net::websocket::connection::ptr connection_;
   std::shared_ptr<forge::api::stream::session> session_;
};

} // namespace forge::api::websocket
