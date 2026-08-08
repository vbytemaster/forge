module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
export module forge.api.transport.client;

export import forge.api.core.connection;
export import forge.api.stream.session;
export import forge.api.transport.exceptions;
export import forge.api.transport.options;
export import forge.net.transport.stream;

export namespace forge::api::transport {

class client final : public forge::api::core::remote_invoker {
 public:
   client();
   client(forge::net::transport::stream stream, options value = {});
   client(std::shared_ptr<forge::api::stream::session> session,
          forge::api::core::descriptor descriptor);
   ~client() override;

   client(client&&) noexcept;
   client& operator=(client&&) noexcept;

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const options& settings() const noexcept;

   boost::asio::awaitable<forge::api::core::response>
   async_call(forge::api::core::request value) override;
   boost::asio::awaitable<forge::api::core::response>
   async_stream_call(
      forge::api::core::request value, forge::api::core::method_kind kind,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> output)
      override;
   boost::asio::awaitable<void> async_close();
   void cancel() noexcept;

 private:
   static boost::asio::awaitable<forge::api::core::response>
   async_call_impl(
      std::shared_ptr<forge::api::stream::session> session,
      std::optional<forge::api::core::descriptor> descriptor,
      forge::api::core::request value);
   static boost::asio::awaitable<forge::api::core::response>
   async_stream_call_impl(
      std::shared_ptr<forge::api::stream::session> session,
      std::optional<forge::api::core::descriptor> descriptor,
      forge::api::core::request value, forge::api::core::method_kind kind,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> output);
   static boost::asio::awaitable<void>
   async_close_impl(std::shared_ptr<forge::api::stream::session> session);

   std::shared_ptr<forge::api::stream::session> session_;
   std::optional<forge::api::core::descriptor> descriptor_;

};

} // namespace forge::api::transport
