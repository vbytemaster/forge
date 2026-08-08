module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <utility>

export module forge.api.core.server_stream_call;

export import forge.api.core.call_options;
export import forge.api.core.stream_reader;

export namespace forge::api::core {

template <typename Out>
class server_stream_call {
 public:
   server_stream_call() = default;
   ~server_stream_call() {
      cancel();
   }

   server_stream_call(server_stream_call&& other) noexcept
       : output_{std::move(other.output_)},
         operation_{std::exchange(other.operation_, {})} {}

   server_stream_call& operator=(server_stream_call&& other) noexcept {
      if (this != &other) {
         cancel();
         output_ = std::move(other.output_);
         operation_ = std::exchange(other.operation_, {});
      }
      return *this;
   }

   server_stream_call(const server_stream_call&) = delete;
   server_stream_call& operator=(const server_stream_call&) = delete;

   boost::asio::awaitable<std::optional<Out>> async_read() {
      return output_.async_read();
   }

   boost::asio::awaitable<void> async_finish() {
      return async_finish_impl(operation_);
   }

   void cancel() noexcept {
      if (operation_) {
         operation_->cancel();
      }
   }

 private:
   static boost::asio::awaitable<void>
   async_finish_impl(std::shared_ptr<detail::call_operation> operation) {
      if (!operation) {
         throw exceptions::protocol_error{"invalid server stream call"};
      }
      static_cast<void>(co_await operation->async_finish());
   }
   server_stream_call(stream_reader<Out> output,
                      std::shared_ptr<detail::call_operation> operation)
       : output_{std::move(output)}, operation_{std::move(operation)} {}

   stream_reader<Out> output_;
   std::shared_ptr<detail::call_operation> operation_;

   friend class detail::call_factory;
};

} // namespace forge::api::core
