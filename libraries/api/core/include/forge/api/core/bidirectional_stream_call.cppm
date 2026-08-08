module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <utility>

export module forge.api.core.bidirectional_stream_call;

export import forge.api.core.call_options;
export import forge.api.core.stream_reader;
export import forge.api.core.stream_writer;

export namespace forge::api::core {

template <typename In, typename Out>
class bidirectional_stream_call {
 public:
   bidirectional_stream_call() = default;
   ~bidirectional_stream_call() {
      cancel();
   }

   bidirectional_stream_call(bidirectional_stream_call&& other) noexcept
       : input_{std::move(other.input_)},
         output_{std::move(other.output_)},
         operation_{std::exchange(other.operation_, {})} {}

   bidirectional_stream_call&
   operator=(bidirectional_stream_call&& other) noexcept {
      if (this != &other) {
         cancel();
         input_ = std::move(other.input_);
         output_ = std::move(other.output_);
         operation_ = std::exchange(other.operation_, {});
      }
      return *this;
   }

   bidirectional_stream_call(const bidirectional_stream_call&) = delete;
   bidirectional_stream_call&
   operator=(const bidirectional_stream_call&) = delete;

   boost::asio::awaitable<void> async_write(In value) {
      return input_.async_write(std::move(value));
   }

   boost::asio::awaitable<std::optional<Out>> async_read() {
      return output_.async_read();
   }

   boost::asio::awaitable<void> async_close() {
      return input_.async_close();
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
         throw exceptions::protocol_error{
            "invalid bidirectional stream call"};
      }
      static_cast<void>(co_await operation->async_finish());
   }
   bidirectional_stream_call(
      stream_writer<In> input, stream_reader<Out> output,
      std::shared_ptr<detail::call_operation> operation)
       : input_{std::move(input)}, output_{std::move(output)},
         operation_{std::move(operation)} {}

   stream_writer<In> input_;
   stream_reader<Out> output_;
   std::shared_ptr<detail::call_operation> operation_;

   friend class detail::call_factory;
};

} // namespace forge::api::core
