module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.api.descriptor;

export import fcl.api.exceptions;
export import fcl.api.types;
export import fcl.exception.exception;
export import fcl.raw.raw;

export namespace fcl::api {

struct error_options {
   status status_code = status::internal;
   bool retryable = false;
};

struct error_descriptor {
   std::string name;
   error_identity identity;
   status status_code = status::internal;
   bool retryable = false;
   std::type_index exception_type = typeid(void);
   std::type_index details_type = typeid(void);
   std::function<void(const error_payload&)> thrower;
};

struct method_descriptor {
   std::string name;
   method_kind kind = method_kind::unary;
   std::type_index request_type = typeid(void);
   std::type_index response_type = typeid(void);
   std::vector<error_descriptor> errors;
   std::function<boost::asio::awaitable<bytes>(std::shared_ptr<void>, bytes)> raw_invoker;
   std::function<boost::asio::awaitable<std::vector<bytes>>(std::shared_ptr<void>, bytes)> raw_stream_invoker;
   std::function<boost::asio::awaitable<bytes>(std::shared_ptr<void>, std::vector<bytes>)>
      raw_client_stream_invoker;
   std::function<boost::asio::awaitable<std::vector<bytes>>(std::shared_ptr<void>, std::vector<bytes>)>
      raw_bidirectional_stream_invoker;
};

struct descriptor {
   api_id id;
   api_version version;
   std::type_index interface_type = typeid(void);
   std::vector<method_descriptor> methods;
};

[[nodiscard]] bool compatible(const descriptor& available, const api_ref& requested) noexcept;
[[nodiscard]] const method_descriptor* find_method(const descriptor& api, std::string_view name) noexcept;

template <typename Exception>
error_identity exception_identity() {
   static_assert(std::is_base_of_v<fcl::exception::base, Exception>,
                 "API errors must derive from fcl::exception::base");
   const auto code = fcl::exception::make_error_code(Exception::value);
   return error_identity{.category = code.category().name(), .code = static_cast<std::uint32_t>(code.value())};
}

template <typename Interface> class method_builder;

template <typename Interface> class contract_builder {
 public:
   explicit contract_builder(descriptor value) : descriptor_(std::move(value)) {}

   template <auto Method, typename Request, typename Response> method_builder<Interface> method(std::string name) {
      return add_method<Method, Request, Response>(std::move(name), method_kind::unary);
   }

   template <auto Method, typename Request, typename Response>
   method_builder<Interface> server_stream(std::string name) {
      for (const auto& existing : descriptor_.methods) {
         if (existing.name == name) {
            throw exceptions::protocol_error{"duplicate API method: " + name};
         }
      }
      descriptor_.methods.push_back(method_descriptor{
          .name = std::move(name),
          .kind = method_kind::server_stream,
          .request_type = typeid(Request),
          .response_type = typeid(Response),
          .raw_stream_invoker =
              [](std::shared_ptr<void> implementation, bytes payload) -> boost::asio::awaitable<std::vector<bytes>> {
             auto typed = std::static_pointer_cast<Interface>(std::move(implementation));
             auto request = fcl::raw::unpack<Request>(payload);
             auto responses = co_await std::invoke(Method, *typed, std::move(request));
             auto packed = std::vector<bytes>{};
             packed.reserve(responses.size());
             for (const auto& response : responses) {
                auto item = bytes{};
                fcl::raw::pack(item, response);
                packed.push_back(std::move(item));
             }
             co_return packed;
          },
      });
      return method_builder<Interface>{*this, descriptor_.methods.back()};
   }

   template <auto Method, typename Request, typename Response>
   method_builder<Interface> client_stream(std::string name) {
      for (const auto& existing : descriptor_.methods) {
         if (existing.name == name) {
            throw exceptions::protocol_error{"duplicate API method: " + name};
         }
      }
      descriptor_.methods.push_back(method_descriptor{
          .name = std::move(name),
          .kind = method_kind::client_stream,
          .request_type = typeid(Request),
          .response_type = typeid(Response),
          .raw_client_stream_invoker =
              [](std::shared_ptr<void> implementation, std::vector<bytes> payloads) -> boost::asio::awaitable<bytes> {
             auto typed = std::static_pointer_cast<Interface>(std::move(implementation));
             auto requests = std::vector<Request>{};
             requests.reserve(payloads.size());
             for (auto& payload : payloads) {
                requests.push_back(fcl::raw::unpack<Request>(payload));
             }
             auto response = co_await std::invoke(Method, *typed, std::move(requests));
             auto packed = bytes{};
             fcl::raw::pack(packed, response);
             co_return packed;
          },
      });
      return method_builder<Interface>{*this, descriptor_.methods.back()};
   }

   template <auto Method, typename Request, typename Response>
   method_builder<Interface> bidirectional_stream(std::string name) {
      for (const auto& existing : descriptor_.methods) {
         if (existing.name == name) {
            throw exceptions::protocol_error{"duplicate API method: " + name};
         }
      }
      descriptor_.methods.push_back(method_descriptor{
          .name = std::move(name),
          .kind = method_kind::bidirectional_stream,
          .request_type = typeid(Request),
          .response_type = typeid(Response),
          .raw_bidirectional_stream_invoker =
              [](std::shared_ptr<void> implementation,
                 std::vector<bytes> payloads) -> boost::asio::awaitable<std::vector<bytes>> {
             auto typed = std::static_pointer_cast<Interface>(std::move(implementation));
             auto requests = std::vector<Request>{};
             requests.reserve(payloads.size());
             for (auto& payload : payloads) {
                requests.push_back(fcl::raw::unpack<Request>(payload));
             }
             auto responses = co_await std::invoke(Method, *typed, std::move(requests));
             auto packed = std::vector<bytes>{};
             packed.reserve(responses.size());
             for (const auto& response : responses) {
                auto item = bytes{};
                fcl::raw::pack(item, response);
                packed.push_back(std::move(item));
             }
             co_return packed;
          },
      });
      return method_builder<Interface>{*this, descriptor_.methods.back()};
   }

 private:
   template <auto Method, typename Request, typename Response>
   method_builder<Interface> add_method(std::string name, method_kind kind) {
      for (const auto& existing : descriptor_.methods) {
         if (existing.name == name) {
            throw exceptions::protocol_error{"duplicate API method: " + name};
         }
      }
      descriptor_.methods.push_back(method_descriptor{
          .name = std::move(name),
          .kind = kind,
          .request_type = typeid(Request),
          .response_type = typeid(Response),
          .raw_invoker =
              [](std::shared_ptr<void> implementation, bytes payload) -> boost::asio::awaitable<bytes> {
             auto typed = std::static_pointer_cast<Interface>(std::move(implementation));
             auto request = fcl::raw::unpack<Request>(payload);
             auto response = co_await std::invoke(Method, *typed, std::move(request));
             auto packed = bytes{};
             fcl::raw::pack(packed, response);
             co_return packed;
          },
      });
      return method_builder<Interface>{*this, descriptor_.methods.back()};
   }

 public:
   [[nodiscard]] descriptor build() {
      if (descriptor_.id.value.empty()) {
         throw exceptions::protocol_error{"API id must not be empty"};
      }
      if (descriptor_.version.major == 0) {
         throw exceptions::protocol_error{"API major version must not be zero"};
      }
      return std::move(descriptor_);
   }

   operator descriptor() {
      return build();
   }

 private:
   descriptor descriptor_;

   friend class method_builder<Interface>;
};

template <typename Interface> class method_builder {
 public:
   method_builder(contract_builder<Interface>& owner, method_descriptor& method) : owner_(&owner), method_(&method) {}

   template <typename Exception, typename Details = void>
   method_builder& error(std::string name, error_options options = {}) {
      method_->errors.push_back(error_descriptor{
          .name = std::move(name),
          .identity = exception_identity<Exception>(),
          .status_code = options.status_code,
          .retryable = options.retryable,
          .exception_type = typeid(Exception),
          .details_type = typeid(Details),
          .thrower =
              [](const error_payload& payload) -> void {
             throw Exception{payload.message,
                             fcl::exception::make_fields(
                                 fcl::exception::ctx("remote.category", payload.identity.category),
                                 fcl::exception::ctx("remote.code", payload.identity.code))};
          },
      });
      return *this;
   }

   template <auto Method, typename Request, typename Response> method_builder method(std::string name) {
      return owner_->template method<Method, Request, Response>(std::move(name));
   }

   template <auto Method, typename Request, typename Response> method_builder client_stream(std::string name) {
      return owner_->template client_stream<Method, Request, Response>(std::move(name));
   }

   template <auto Method, typename Request, typename Response> method_builder bidirectional_stream(std::string name) {
      return owner_->template bidirectional_stream<Method, Request, Response>(std::move(name));
   }

   [[nodiscard]] descriptor build() {
      return owner_->build();
   }

   operator descriptor() {
      return build();
   }

 private:
   contract_builder<Interface>* owner_ = nullptr;
   method_descriptor* method_ = nullptr;
};

template <typename Interface> contract_builder<Interface> contract(descriptor value) {
   value.interface_type = typeid(Interface);
   return contract_builder<Interface>{std::move(value)};
}

} // namespace fcl::api
