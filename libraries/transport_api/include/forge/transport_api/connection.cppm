module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module forge.transport.api.connection;

export import forge.api.connection;
export import forge.transport.api.client;

export namespace forge::transport::api {

class connection final : public forge::api::connection {
 public:
   connection();
   connection(forge::transport::stream stream, options value = {});
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
   struct impl;

   boost::asio::awaitable<std::shared_ptr<forge::api::remote_invoker>>
   open_remote_invoker(forge::api::api_ref requested, forge::api::descriptor remote_descriptor) override;

   std::shared_ptr<impl> impl_;
};

} // namespace forge::transport::api
