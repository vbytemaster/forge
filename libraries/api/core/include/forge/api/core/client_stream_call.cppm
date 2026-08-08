module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <type_traits>
#include <utility>

export module forge.api.core.client_stream_call;

export import forge.api.core.call_options;
export import forge.api.core.stream_writer;

export namespace forge::api::core {

template <typename In, typename Result>
class client_stream_call {
 public:
   client_stream_call() = default;
   ~client_stream_call() {
      cancel();
   }

   client_stream_call(client_stream_call&& other) noexcept
       : input_{std::move(other.input_)},
         operation_{std::exchange(other.operation_, {})} {}

   client_stream_call& operator=(client_stream_call&& other) noexcept {
      if (this != &other) {
         cancel();
         input_ = std::move(other.input_);
         operation_ = std::exchange(other.operation_, {});
      }
      return *this;
   }

   client_stream_call(const client_stream_call&) = delete;
   client_stream_call& operator=(const client_stream_call&) = delete;

   boost::asio::awaitable<void> async_write(In value) {
      return input_.async_write(std::move(value));
   }

   boost::asio::awaitable<void> async_close() {
      return input_.async_close();
   }

   boost::asio::awaitable<Result> async_finish() {
      return async_finish_impl(operation_);
   }

   void cancel() noexcept {
      if (operation_) {
         operation_->cancel();
      }
   }

 private:
   static boost::asio::awaitable<Result>
   async_finish_impl(std::shared_ptr<detail::call_operation> operation) {
      if (!operation) {
         throw exceptions::protocol_error{"invalid client stream call"};
      }
      auto result = co_await operation->async_finish();
      if constexpr (std::same_as<Result, void>) {
         co_return;
      } else {
         co_return result.template take<Result>();
      }
   }
   client_stream_call(stream_writer<In> input,
                      std::shared_ptr<detail::call_operation> operation)
       : input_{std::move(input)}, operation_{std::move(operation)} {}

   stream_writer<In> input_;
   std::shared_ptr<detail::call_operation> operation_;

   friend class detail::call_factory;
};

} // namespace forge::api::core
