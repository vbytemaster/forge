module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.api.core.handle;

export import forge.api.core.bidirectional_stream_call;
export import forge.api.core.client_stream_call;
export import forge.api.core.descriptor;
export import forge.api.core.server_stream_call;

export namespace forge::api::core {

namespace detail {

template <auto Method, method_kind Kind = method_kind_v<Method>>
struct method_call;

template <auto Method>
struct method_call<Method, method_kind::server_stream> {
   using endpoint_type = std::remove_cvref_t<method_argument_t<
      Method, method_argument_count_v<Method> - 1>>;
   using type = server_stream_call<
      typename stream_writer_traits<endpoint_type>::item_type>;
};

template <auto Method>
struct method_call<Method, method_kind::client_stream> {
   using endpoint_type = std::remove_cvref_t<method_argument_t<
      Method, method_argument_count_v<Method> - 1>>;
   using type = client_stream_call<
      typename stream_reader_traits<endpoint_type>::item_type,
      method_response_t<Method>>;
};

template <auto Method>
struct method_call<Method, method_kind::bidirectional_stream> {
   using endpoint_type = std::remove_cvref_t<method_argument_t<
      Method, method_argument_count_v<Method> - 1>>;
   using type = bidirectional_stream_call<
      typename duplex_stream_traits<endpoint_type>::input_type,
      typename duplex_stream_traits<endpoint_type>::output_type>;
};

class call_access {
 public:
   template <auto Method, typename Interface, typename... Args>
   static boost::asio::awaitable<typename method_call<Method>::type>
   async_open(std::shared_ptr<Interface> implementation, call_options options,
              Args&&... args) {
      static_assert(method_kind_v<Method> != method_kind::unary,
                    "async_open requires a streaming API method");
      static_assert(sizeof...(Args) == fixed_argument_count_v<Method>,
                    "async_open fixed argument count does not match method signature");
      validate_method_signature<Method>();
      if (!implementation) {
         throw exceptions::protocol_error{"invalid API handle"};
      }

      const auto executor = co_await boost::asio::this_coro::executor;
      using argument_tuple = detail::method_fixed_argument_tuple_t<Method>;
      auto arguments = argument_tuple{std::forward<Args>(args)...};

      if constexpr (method_kind_v<Method> == method_kind::server_stream) {
         using call_type = typename method_call<Method>::type;
         using endpoint_type = typename method_call<Method>::endpoint_type;
         using item_type = typename stream_writer_traits<endpoint_type>::item_type;
         auto pipe = make_pipe(executor, options);
         auto operation = make_call_operation(
            executor, {pipe.reader, pipe.writer}, options.deadline);
         auto output = reader_access::make<item_type>(pipe.reader);
         auto writer = writer_access::make<item_type>(pipe.writer);
         operation->start(run_server<Method>(
            std::move(implementation), std::move(arguments), std::move(writer)));
         co_return call_factory::make<call_type>(
            std::move(output), std::move(operation));
      } else if constexpr (method_kind_v<Method> == method_kind::client_stream) {
         using call_type = typename method_call<Method>::type;
         using endpoint_type = typename method_call<Method>::endpoint_type;
         using item_type = typename stream_reader_traits<endpoint_type>::item_type;
         auto pipe = make_pipe(executor, options);
         auto operation = make_call_operation(
            executor, {pipe.reader, pipe.writer}, options.deadline);
         auto input = writer_access::make<item_type>(pipe.writer);
         auto reader = reader_access::make<item_type>(pipe.reader);
         operation->start(run_client<Method>(
            std::move(implementation), std::move(arguments), std::move(reader)));
         co_return call_factory::make<call_type>(
            std::move(input), std::move(operation));
      } else {
         using call_type = typename method_call<Method>::type;
         using endpoint_type = typename method_call<Method>::endpoint_type;
         using input_type = typename duplex_stream_traits<endpoint_type>::input_type;
         using output_type = typename duplex_stream_traits<endpoint_type>::output_type;
         auto incoming = make_pipe(executor, options);
         auto outgoing = make_pipe(executor, options);
         auto operation = make_call_operation(
            executor,
            {incoming.reader, incoming.writer, outgoing.reader, outgoing.writer},
            options.deadline);
         auto input = writer_access::make<input_type>(incoming.writer);
         auto output = reader_access::make<output_type>(outgoing.reader);
         auto stream = duplex_stream<input_type, output_type>{
            reader_access::make<input_type>(incoming.reader),
            writer_access::make<output_type>(outgoing.writer)};
         operation->start(run_bidirectional<Method>(
            std::move(implementation), std::move(arguments), std::move(stream)));
         co_return call_factory::make<call_type>(
            std::move(input), std::move(output), std::move(operation));
      }
   }

 private:
   [[nodiscard]] static local_stream_pair
   make_pipe(const boost::asio::any_io_executor& executor,
             const call_options& options) {
      return make_local_stream_pair(
         executor, options.max_item_bytes, options.max_buffered_items,
         options.max_buffered_bytes);
   }

   template <auto Method, typename Interface, typename Tuple, typename Writer>
   static boost::asio::awaitable<call_result>
   run_server(std::shared_ptr<Interface> implementation, Tuple arguments,
              Writer writer) {
      co_await invoke_stream<Method>(
         *implementation, std::move(arguments), std::move(writer),
         std::make_index_sequence<fixed_argument_count_v<Method>>{});
      co_return call_result{};
   }

   template <auto Method, typename Interface, typename Tuple, typename Reader>
   static boost::asio::awaitable<call_result>
   run_client(std::shared_ptr<Interface> implementation, Tuple arguments,
              Reader reader) {
      if constexpr (std::same_as<method_response_t<Method>, void>) {
         co_await invoke_stream<Method>(
            *implementation, std::move(arguments), std::move(reader),
            std::make_index_sequence<fixed_argument_count_v<Method>>{});
         co_return call_result{};
      } else {
         auto result = co_await invoke_stream<Method>(
            *implementation, std::move(arguments), std::move(reader),
            std::make_index_sequence<fixed_argument_count_v<Method>>{});
         co_return call_result::make(std::move(result));
      }
   }

   template <auto Method, typename Interface, typename Tuple, typename Stream>
   static boost::asio::awaitable<call_result>
   run_bidirectional(std::shared_ptr<Interface> implementation, Tuple arguments,
                     Stream stream) {
      co_await invoke_stream<Method>(
         *implementation, std::move(arguments), std::move(stream),
         std::make_index_sequence<fixed_argument_count_v<Method>>{});
      co_return call_result{};
   }
};

} // namespace detail

template <typename Interface>
class handle {
 public:
   handle() = default;
   explicit handle(std::shared_ptr<Interface> local)
       : local_{std::move(local)} {}

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(local_);
   }

   [[nodiscard]] Interface* operator->() const noexcept {
      return local_.get();
   }

   [[nodiscard]] std::shared_ptr<Interface> shared() const noexcept {
      return local_;
   }

   template <auto Method, typename... Args>
   boost::asio::awaitable<typename detail::method_call<Method>::type>
   async_open(Args&&... args) const {
      static_assert(std::same_as<method_class_t<Method>, Interface>,
                    "async_open method pointer must belong to the handle interface");
      co_return co_await detail::call_access::async_open<Method>(
         local_, call_options{}, std::forward<Args>(args)...);
   }

   template <auto Method, typename... Args>
   boost::asio::awaitable<typename detail::method_call<Method>::type>
   async_open(call_options options, Args&&... args) const {
      static_assert(std::same_as<method_class_t<Method>, Interface>,
                    "async_open method pointer must belong to the handle interface");
      co_return co_await detail::call_access::async_open<Method>(
         local_, std::move(options), std::forward<Args>(args)...);
   }

 private:
   std::shared_ptr<Interface> local_;
};

} // namespace forge::api::core
