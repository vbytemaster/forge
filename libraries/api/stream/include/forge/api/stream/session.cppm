module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>

export module forge.api.stream.session;

export import forge.api.core.dispatcher;
export import forge.api.stream.options;
export import forge.net.transport.stream;

export namespace forge::api::stream {

class session {
 public:
   session();
   session(forge::net::transport::stream stream, options value = {});
   session(forge::net::transport::stream stream,
           forge::api::core::binding_plan plan, options value = {},
           forge::api::core::metadata trusted_metadata = {});
   ~session();

   session(session&&) noexcept;
   session& operator=(session&&) noexcept;

   session(const session&) = delete;
   session& operator=(const session&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const options& settings() const noexcept;

   boost::asio::awaitable<forge::api::core::frame>
   async_call(
      forge::api::core::frame request, call_options value = {},
      std::optional<forge::api::core::method_descriptor> descriptor =
         std::nullopt);

   boost::asio::awaitable<forge::api::core::frame>
   async_stream_call(
      forge::api::core::frame request, forge::api::core::method_kind kind,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> output,
      call_options value = {},
      std::optional<forge::api::core::method_descriptor> descriptor =
         std::nullopt);

   boost::asio::awaitable<void> async_serve();
   boost::asio::awaitable<void> async_close();
   void cancel() noexcept;

 private:
   struct impl;

   static boost::asio::awaitable<forge::api::core::frame>
   async_call_impl(
      std::shared_ptr<impl> self, forge::api::core::frame request,
      forge::api::core::method_kind kind,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
      std::shared_ptr<forge::api::core::detail::stream_endpoint> output,
      call_options value,
      std::optional<forge::api::core::method_descriptor> descriptor);
   static boost::asio::awaitable<void>
   async_serve_impl(std::shared_ptr<impl> self);
   static boost::asio::awaitable<void>
   async_close_impl(std::shared_ptr<impl> self);

   std::shared_ptr<impl> impl_;
};

} // namespace forge::api::stream
