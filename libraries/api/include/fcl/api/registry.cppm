module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

export module fcl.api.registry;

export import fcl.api.descriptor;
export import fcl.api.error_projection;
export import fcl.api.handle;

export namespace fcl::api {

class registry {
 public:
   registry();
   ~registry();

   registry(const registry&) = delete;
   registry& operator=(const registry&) = delete;

   template <typename Interface> void install(descriptor descriptor, std::shared_ptr<Interface> implementation) {
      static_assert(std::is_class_v<Interface>, "API interface must be a class type");
      static_assert(std::has_virtual_destructor_v<Interface>, "API interface must have a virtual destructor");
      if (!implementation) {
         throw exceptions::protocol_error{"cannot install null API implementation"};
      }
      if (!descriptor.interface_type.hash_code() || descriptor.interface_type != typeid(Interface)) {
         descriptor.interface_type = typeid(Interface);
      }
      const auto key = key_for(descriptor.id.value, descriptor.version.major);
      if (entries_.contains(key)) {
         throw exceptions::protocol_error{"duplicate API implementation"};
      }
      entries_.emplace(key, entry{std::move(descriptor), std::move(implementation), typeid(Interface)});
   }

   template <typename Interface> [[nodiscard]] handle<Interface> try_get(api_ref requested) const {
      const auto* entry = find(requested);
      if (entry == nullptr || entry->interface_type != typeid(Interface)) {
         return {};
      }
      return handle<Interface>{std::static_pointer_cast<Interface>(entry->implementation)};
   }

   template <typename Interface> [[nodiscard]] handle<Interface> get(api_ref requested) const {
      auto result = try_get<Interface>(std::move(requested));
      if (!result) {
         throw exceptions::protocol_error{"required API is not available"};
      }
      return result;
   }

   [[nodiscard]] const descriptor* describe(api_ref requested) const noexcept;
   boost::asio::awaitable<frame> dispatch(frame request) const;
   boost::asio::awaitable<std::vector<frame>> dispatch_many(frame request) const;
   boost::asio::awaitable<std::vector<frame>> dispatch_stream(std::vector<frame> frames) const;
   [[nodiscard]] std::size_t size() const noexcept;
   void clear() noexcept;

 private:
   struct entry {
      descriptor descriptor;
      std::shared_ptr<void> implementation;
      std::type_index interface_type = typeid(void);
   };

   static std::string key_for(std::string_view id, std::uint16_t major);
   [[nodiscard]] const entry* find(api_ref requested) const noexcept;

   std::unordered_map<std::string, entry> entries_;
};

class installer {
 public:
   explicit installer(registry& apis) : apis_(&apis) {}

   template <typename Interface> void install(descriptor descriptor, std::shared_ptr<Interface> implementation) {
      apis_->install<Interface>(std::move(descriptor), std::move(implementation));
   }

 private:
   registry* apis_ = nullptr;
};

using provider = installer;

class view {
 public:
   explicit view(const registry& apis) : apis_(&apis) {}

   template <typename Interface> [[nodiscard]] handle<Interface> try_get(api_ref requested) const {
      return apis_->try_get<Interface>(std::move(requested));
   }

   template <typename Interface> [[nodiscard]] handle<Interface> get(api_ref requested) const {
      return apis_->get<Interface>(std::move(requested));
   }

   [[nodiscard]] const registry& registry_ref() const noexcept {
      return *apis_;
   }

 private:
   const registry* apis_ = nullptr;
};

} // namespace fcl::api
