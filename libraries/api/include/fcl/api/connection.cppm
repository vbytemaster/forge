module;

#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>

export module fcl.api.connection;

export import fcl.api.descriptor;
export import fcl.api.error_projection;
export import fcl.api.handle;

export namespace fcl::api {

template <typename Interface> struct api_traits;
template <typename Interface> class proxy;

enum class surface : std::uint8_t {
   none = 0,
   local = 1,
   remote = 2,
};

[[nodiscard]] constexpr surface operator|(surface lhs, surface rhs) noexcept {
   return static_cast<surface>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr surface operator&(surface lhs, surface rhs) noexcept {
   return static_cast<surface>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool supports(surface value, surface expected) noexcept {
   return (static_cast<std::uint8_t>(value & expected) == static_cast<std::uint8_t>(expected));
}

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

   template <typename Request, typename Response>
   boost::asio::awaitable<Response> call(const descriptor& contract, api_ref api, std::string method, Request value) {
      auto outbound = request{
          .api = std::move(api),
          .method = std::move(method),
          .codec = {.value = "fcl.raw"},
          .body = pack_body(value),
      };
      auto inbound = co_await async_call(std::move(outbound));
      if (inbound.error) {
         raise_remote_error(*inbound.error, find_method(contract, inbound.method));
      }
      co_return unpack_body<Response>(inbound.body);
   }
};

namespace detail {

template <typename Interface, bool Remote> class proxy_impl;

template <typename Interface> class proxy_impl<Interface, false> {
 public:
   explicit proxy_impl(std::shared_ptr<remote_invoker>) {}
};

} // namespace detail

class service_mount {
 public:
   virtual ~service_mount() = default;

   template <typename Interface>
   void register_api(std::shared_ptr<Interface> implementation) {
      static_assert(local_interface<Interface>, "Interface must opt in to fcl::api::surface::local");
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
      static_assert(remote_interface<Interface>, "Interface must opt in to fcl::api::surface::remote");
      auto remote_descriptor = Interface::describe();
      auto invoker = co_await open_remote_invoker(requested, remote_descriptor);
      co_return handle<Interface>{std::make_shared<proxy<Interface>>(std::move(invoker))};
   }

 protected:
   virtual boost::asio::awaitable<std::shared_ptr<remote_invoker>> open_remote_invoker(api_ref requested,
                                                                                       descriptor remote_descriptor) = 0;
};

class connection : public remote_mount {
 public:
   ~connection() override = default;

   virtual void cancel() noexcept = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
};

} // namespace fcl::api
