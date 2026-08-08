module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeindex>
#include <tuple>
#include <utility>

export module forge.api.core.connection;

export import forge.api.core.descriptor;
export import forge.api.core.error_projection;
export import forge.api.core.handle;

export namespace forge::api::core {

template <typename Interface> struct api_traits;
template <typename Interface> class proxy;

template <typename Interface, surface Surface = surface::local> class contract {
 public:
   using interface = Interface;
   static constexpr auto api_surface = Surface;

   [[nodiscard]] static descriptor describe() {
      return api_traits<Interface>::describe();
   }

   [[nodiscard]] static api_ref ref(std::uint16_t min_revision = api_traits<Interface>::version().revision) {
      const auto version = api_traits<Interface>::version();
      return api_ref{.id = api_traits<Interface>::id(), .major = version.major, .min_revision = min_revision};
   }
};

template <typename T>
concept interface = requires {
   typename T::interface;
   { T::api_surface } -> std::convertible_to<surface>;
} && std::derived_from<T, contract<typename T::interface, T::api_surface>>;

template <typename T, surface Surface>
concept supports_surface = interface<T> && supports(T::api_surface, Surface);

template <typename T>
concept local_interface = supports_surface<T, surface::local>;

template <typename T>
concept remote_interface = supports_surface<T, surface::remote>;

class remote_invoker {
 public:
   virtual ~remote_invoker() = default;

   virtual boost::asio::awaitable<response> async_call(request value) = 0;

   virtual boost::asio::awaitable<response>
   async_stream_call(request value, method_kind kind, std::shared_ptr<detail::stream_endpoint> input,
                     std::shared_ptr<detail::stream_endpoint> output) {
      static_cast<void>(value);
      static_cast<void>(kind);
      static_cast<void>(input);
      static_cast<void>(output);
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "remote invoker does not support incremental stream calls");
   }

   virtual bool supports_typed_arguments() const noexcept {
      return false;
   }

   virtual boost::asio::awaitable<void> async_call_arguments(request value, std::type_index argument_tuple_type,
                                                             void* argument_tuple, std::type_index response_type,
                                                            void* response_storage) {
      static_cast<void>(value);
      static_cast<void>(argument_tuple_type);
      static_cast<void>(argument_tuple);
      static_cast<void>(response_type);
      static_cast<void>(response_storage);
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                          "remote invoker does not support typed argument calls");
   }

   template <typename Request, typename Response>
   boost::asio::awaitable<Response> call(const descriptor& contract, api_ref api, std::string method, Request value) {
      auto outbound = request{
          .api = std::move(api),
          .method = std::move(method),
          .codec = {.value = "forge.raw"},
          .body = pack_body(value),
      };
      auto inbound = co_await async_call(std::move(outbound));
      if (inbound.error) {
         raise_remote_error(*inbound.error, find_method(contract, inbound.method));
      }
      co_return unpack_body<Response>(inbound.body);
   }

   template <typename Response, typename... Args>
   boost::asio::awaitable<Response> call_arguments(const descriptor& contract, api_ref api, std::string method,
                                                   Args&&... args) {
      using argument_tuple = std::tuple<std::remove_cvref_t<Args>...>;
      if (supports_typed_arguments()) {
         auto arguments = argument_tuple{std::forward<Args>(args)...};
         auto output = std::optional<Response>{};
         auto outbound = request{
             .api = std::move(api),
             .method = std::move(method),
             .codec = {.value = "forge.typed"},
         };
         co_await async_call_arguments(std::move(outbound), typeid(argument_tuple), &arguments, typeid(Response),
                                       &output);
         if (!output.has_value()) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                "typed remote invoker returned no response value");
         }
         co_return std::move(*output);
      }

      auto outbound = request{
          .api = std::move(api),
          .method = std::move(method),
          .codec = {.value = "forge.raw"},
      };
      if constexpr (sizeof...(Args) == 1U) {
         outbound.body = pack_body((std::forward<Args>(args), ...));
      } else {
         outbound.body = pack_body(std::make_tuple(std::forward<Args>(args)...));
      }
      auto inbound = co_await async_call(std::move(outbound));
      if (inbound.error) {
         raise_remote_error(*inbound.error, find_method(contract, inbound.method));
      }
      co_return unpack_body<Response>(inbound.body);
   }
};

namespace detail {

template <auto Method, typename Tuple, std::size_t... Index>
[[nodiscard]] bytes pack_fixed_proxy_arguments(const Tuple& arguments, std::index_sequence<Index...>) {
   constexpr auto count = sizeof...(Index);
   if constexpr (count == 0) {
      return {};
   } else if constexpr (count == 1) {
      return pack_body(std::get<0>(arguments));
   } else {
      return pack_body(std::tuple{std::get<Index>(arguments)...});
   }
}

template <typename Interface, typename Request, typename Response>
boost::asio::awaitable<Response> proxy_call(std::shared_ptr<remote_invoker> invoker, api_ref selected_api,
                                            std::string method, Request request) {
   if constexpr (remote_interface<Interface>) {
      co_return co_await invoker->template call<Request, Response>(
          api_traits<Interface>::describe(), std::move(selected_api), std::move(method), std::move(request));
   } else {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "local-only API does not support remote invocation");
   }
}

template <typename Interface, typename Response, typename... Args>
boost::asio::awaitable<Response> proxy_call_arguments(std::shared_ptr<remote_invoker> invoker, api_ref selected_api,
                                                      std::string method, Args&&... args) {
   if constexpr (remote_interface<Interface>) {
      co_return co_await invoker->template call_arguments<Response>(
          api_traits<Interface>::describe(), std::move(selected_api), std::move(method), std::forward<Args>(args)...);
   } else {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "local-only API does not support remote invocation");
   }
}

template <typename Interface, auto Method, typename... Args>
boost::asio::awaitable<method_response_t<Method>> proxy_method(std::shared_ptr<remote_invoker> invoker,
                                                               api_ref selected_api, std::string method,
                                                               Args&&... args) {
   validate_method_signature<Method>();
   static_assert(sizeof...(Args) == method_argument_count_v<Method>,
                 "generated API proxy argument count does not match method signature");
   if constexpr (!remote_interface<Interface>) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "local-only API does not support remote invocation");
   } else if constexpr (method_kind_v<Method> == method_kind::unary) {
      co_return co_await proxy_call_arguments<Interface, method_response_t<Method>>(
          std::move(invoker), std::move(selected_api), std::move(method), std::forward<Args>(args)...);
   } else {
      auto arguments = std::tuple<std::remove_cvref_t<Args>...>{std::forward<Args>(args)...};
      auto& endpoint = std::get<method_argument_count_v<Method> - 1>(arguments);
      auto input = std::shared_ptr<stream_endpoint>{};
      auto output = std::shared_ptr<stream_endpoint>{};
      if constexpr (method_kind_v<Method> == method_kind::server_stream) {
         output = writer_access::endpoint(endpoint);
      } else if constexpr (method_kind_v<Method> == method_kind::client_stream) {
         input = reader_access::endpoint(endpoint);
      } else {
         input = reader_access::endpoint(endpoint.input());
         output = writer_access::endpoint(endpoint.output());
      }

      auto outbound = request{
          .api = std::move(selected_api),
          .method = std::move(method),
          .codec = {.value = "forge.raw"},
          .body = pack_fixed_proxy_arguments<Method>(
              arguments, std::make_index_sequence<fixed_argument_count_v<Method>>{}),
      };
      auto inbound = co_await invoker->async_stream_call(std::move(outbound), method_kind_v<Method>, std::move(input),
                                                         std::move(output));
      if (inbound.error) {
         raise_remote_error(*inbound.error, find_method(api_traits<Interface>::describe(), inbound.method));
      }
      if constexpr (std::same_as<method_response_t<Method>, void>) {
         co_return;
      } else {
         co_return unpack_body<method_response_t<Method>>(inbound.body);
      }
   }
}

template <typename Interface, bool Remote> class proxy_impl;

template <typename Interface> class proxy_impl<Interface, false> {
 public:
   explicit proxy_impl(std::shared_ptr<remote_invoker>) {}
   explicit proxy_impl(std::shared_ptr<remote_invoker> invoker, api_ref) : proxy_impl(std::move(invoker)) {}
};

} // namespace detail

class service_mount {
 public:
   virtual ~service_mount() = default;

   template <typename Interface> void register_api(std::shared_ptr<Interface> implementation) {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      register_api(Interface::describe(), std::move(implementation), typeid(Interface));
   }

 protected:
   virtual void register_api(descriptor value, std::shared_ptr<void> implementation, std::type_index type) = 0;
};

class remote_mount {
 public:
   virtual ~remote_mount() = default;

   template <typename Interface>
   boost::asio::awaitable<handle<Interface>> get_remote_api(api_ref requested = Interface::ref()) {
      static_assert(remote_interface<Interface>, "Interface must opt in to forge::api::core::surface::remote");
      auto remote_descriptor = Interface::describe();
      auto invoker = co_await open_remote_invoker(requested, remote_descriptor);
      co_return handle<Interface>{std::make_shared<proxy<Interface>>(std::move(invoker), std::move(requested))};
   }

 protected:
   virtual boost::asio::awaitable<std::shared_ptr<remote_invoker>>
   open_remote_invoker(api_ref requested, descriptor remote_descriptor) = 0;
};

class connection : public remote_mount {
 public:
   ~connection() override = default;

   virtual void cancel() noexcept = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
};

} // namespace forge::api::core
