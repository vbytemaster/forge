module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module fcl.api.transport.connection;

export import fcl.api.connection;
export import fcl.api.transport.client;

export namespace fcl::api::transport {

class connection final : public fcl::api::connection {
 public:
   connection();
   connection(fcl::transport::stream stream, options value = {});
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

   boost::asio::awaitable<std::shared_ptr<fcl::api::remote_invoker>>
   open_remote_invoker(fcl::api::api_ref requested, fcl::api::descriptor remote_descriptor) override;

   std::shared_ptr<impl> impl_;
};

} // namespace fcl::api::transport
