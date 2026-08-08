module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module forge.api.transport.connection;

export import forge.api.core.connection;
export import forge.api.transport.client;

export namespace forge::api::transport {

class connection final : public forge::api::core::connection {
 public:
   connection();
   connection(forge::net::transport::stream stream, options value = {});
   ~connection() override;

   connection(connection&&) noexcept;
   connection& operator=(connection&&) noexcept;

   connection(const connection&) = delete;
   connection& operator=(const connection&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const options& settings() const noexcept;

   boost::asio::awaitable<void> async_close() override;
   void cancel() noexcept override;

 private:
   static boost::asio::awaitable<void>
   async_close_impl(std::shared_ptr<forge::api::stream::session> session);
   static boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
   open_remote_invoker_impl(
      std::shared_ptr<forge::api::stream::session> session,
      forge::api::core::descriptor remote_descriptor);

   boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
   open_remote_invoker(forge::api::core::api_ref requested, forge::api::core::descriptor remote_descriptor) override;

   std::shared_ptr<forge::api::stream::session> session_;
};

} // namespace forge::api::transport
