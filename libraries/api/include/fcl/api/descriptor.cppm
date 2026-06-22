module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

export module fcl.api.descriptor;

export import fcl.api.exceptions;
export import fcl.api.types;
export import fcl.exceptions;
export import fcl.raw.raw;
export import fcl.raw.datastream;

export namespace fcl::api {

template <typename T>
[[nodiscard]] T unpack_body(std::span<const std::uint8_t> body) {
   auto out = T{};
   fcl::datastream<const std::uint8_t*> stream{body.data(), body.size()};
   fcl::raw::unpack(stream, out);
   if (stream.remaining() != 0) {
      throw exceptions::protocol_error{"API body has trailing bytes"};
   }
   return out;
}

template <typename T>
[[nodiscard]] T unpack_body(const bytes& body) {
   return unpack_body<T>(std::span<const std::uint8_t>{body.data(), body.size()});
}

template <typename T>
[[nodiscard]] bytes pack_body(const T& value) {
   auto out = bytes(fcl::raw::pack_size(value));
   if (!out.empty()) {
      fcl::datastream<std::uint8_t*> stream{out.data(), out.size()};
      fcl::raw::pack(stream, value);
   }
   return out;
}

template <typename T> struct method_signature;

template <typename Class, typename Response, typename... Args>
struct method_signature<boost::asio::awaitable<Response> (Class::*)(Args...)> {
   using class_type = Class;
   using argument_tuple = std::tuple<Args...>;
   using response_type = Response;
};

template <typename Class, typename Response, typename... Args>
struct method_signature<boost::asio::awaitable<Response> (Class::*)(Args...) const> {
   using class_type = Class;
   using argument_tuple = std::tuple<Args...>;
   using response_type = Response;
};

template <typename Tuple> struct method_payload;

template <>
struct method_payload<std::tuple<>> {
   using type = std::tuple<>;
};

template <typename T>
struct method_payload<std::tuple<T>> {
   using type = T;
};

template <typename First, typename Second, typename... Rest>
struct method_payload<std::tuple<First, Second, Rest...>> {
   using type = std::tuple<First, Second, Rest...>;
};

template <auto Method>
using method_argument_tuple_t = typename method_signature<decltype(Method)>::argument_tuple;

template <auto Method>
using method_request_t = typename method_payload<method_argument_tuple_t<Method>>::type;

template <auto Method>
using method_response_t = typename method_signature<decltype(Method)>::response_type;

template <auto Method, std::size_t Index>
using method_argument_t = std::tuple_element_t<Index, method_argument_tuple_t<Method>>;

template <auto Method>
inline constexpr auto method_argument_count_v = std::tuple_size_v<method_argument_tuple_t<Method>>;

namespace detail {

[[nodiscard]] inline std::string_view trim_argument_name(std::string_view value) noexcept {
   while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n' ||
                            value.front() == '\r')) {
      value.remove_prefix(1);
   }
   while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\n' ||
                            value.back() == '\r')) {
      value.remove_suffix(1);
   }
   return value;
}

[[nodiscard]] inline std::vector<std::string> argument_names_from_macro(std::string_view value) {
   value = trim_argument_name(value);
   if (value.size() >= 2U && value.front() == '(' && value.back() == ')') {
      value.remove_prefix(1);
      value.remove_suffix(1);
   }
   value = trim_argument_name(value);
   if (value.empty()) {
      return {};
   }

   auto names = std::vector<std::string>{};
   while (true) {
      const auto comma = value.find(',');
      const auto token = trim_argument_name(value.substr(0, comma));
      if (token.empty()) {
         throw exceptions::protocol_error{"API method argument name is empty"};
      }
      names.emplace_back(token);
      if (comma == std::string_view::npos) {
         break;
      }
      value.remove_prefix(comma + 1U);
   }
   return names;
}

} // namespace detail

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
   std::uint16_t since_revision = 0;
   bool deprecated = false;
   std::string deprecation_reason;
   std::type_index request_type = typeid(void);
   std::type_index response_type = typeid(void);
   std::vector<std::string> argument_names;
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
[[nodiscard]] bool compatible(const method_descriptor& available, const method_descriptor& requested) noexcept;
[[nodiscard]] const method_descriptor* find_method(const descriptor& api, std::string_view name) noexcept;

template <typename Exception>
error_identity exception_identity() {
   static_assert(std::is_base_of_v<fcl::exceptions::base, Exception>,
                 "API errors must derive from fcl::exceptions::base");
   const auto code = fcl::exceptions::make_error_code(Exception::value);
   return error_identity{.category = code.category().name(), .code = static_cast<std::uint32_t>(code.value())};
}

template <typename Interface> class method_builder;

template <typename Interface> class contract_builder {
 public:
   explicit contract_builder(descriptor value) : descriptor_(std::move(value)) {}

   template <auto Method> method_builder<Interface> method(std::string name) {
      return add_deduced_method<Method>(std::move(name), {});
   }

   template <auto Method>
   method_builder<Interface> method(std::string name, std::vector<std::string> argument_names) {
      return add_deduced_method<Method>(std::move(name), std::move(argument_names));
   }

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
             auto request = unpack_body<Request>(payload);
             auto responses = co_await std::invoke(Method, *typed, std::move(request));
             auto packed = std::vector<bytes>{};
             packed.reserve(responses.size());
             for (const auto& response : responses) {
                packed.push_back(pack_body(response));
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
                requests.push_back(unpack_body<Request>(payload));
             }
             auto response = co_await std::invoke(Method, *typed, std::move(requests));
             co_return pack_body(response);
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
                requests.push_back(unpack_body<Request>(payload));
             }
             auto responses = co_await std::invoke(Method, *typed, std::move(requests));
             auto packed = std::vector<bytes>{};
             packed.reserve(responses.size());
             for (const auto& response : responses) {
                packed.push_back(pack_body(response));
             }
             co_return packed;
          },
      });
      return method_builder<Interface>{*this, descriptor_.methods.back()};
   }

 private:
   template <auto Method>
   method_builder<Interface> add_deduced_method(std::string name, std::vector<std::string> argument_names) {
      constexpr auto argument_count = method_argument_count_v<Method>;
      if (!argument_names.empty() && argument_names.size() != argument_count) {
         throw exceptions::protocol_error{"API method argument name count does not match method signature: " + name};
      }
      if (argument_names.empty() && argument_count != 1U) {
         throw exceptions::protocol_error{"API positional method requires argument names: " + name};
      }

      using Request = method_request_t<Method>;
      using Response = method_response_t<Method>;
      for (const auto& existing : descriptor_.methods) {
         if (existing.name == name) {
            throw exceptions::protocol_error{"duplicate API method: " + name};
         }
      }
      descriptor_.methods.push_back(method_descriptor{
          .name = std::move(name),
          .kind = method_kind::unary,
          .request_type = typeid(Request),
          .response_type = typeid(Response),
          .argument_names = std::move(argument_names),
          .raw_invoker =
              [](std::shared_ptr<void> implementation, bytes payload) -> boost::asio::awaitable<bytes> {
             auto typed = std::static_pointer_cast<Interface>(std::move(implementation));
             if constexpr (argument_count == 1U) {
                auto request = unpack_body<Request>(payload);
                auto response = co_await std::invoke(Method, *typed, std::move(request));
                co_return pack_body(response);
             } else {
                auto request = unpack_body<method_argument_tuple_t<Method>>(payload);
                auto invoke = [&](auto&&... args) -> boost::asio::awaitable<Response> {
                   co_return co_await std::invoke(Method, *typed, std::forward<decltype(args)>(args)...);
                };
                auto response = co_await std::apply(invoke, std::move(request));
                co_return pack_body(response);
             }
          },
      });
      return method_builder<Interface>{*this, descriptor_.methods.back()};
   }

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
             auto request = unpack_body<Request>(payload);
             auto response = co_await std::invoke(Method, *typed, std::move(request));
             co_return pack_body(response);
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
                             fcl::exceptions::make_fields(
                                 fcl::exceptions::ctx("remote.category", payload.identity.category),
                                 fcl::exceptions::ctx("remote.code", payload.identity.code))};
          },
      });
      return *this;
   }

   method_builder& since_revision(std::uint16_t value) noexcept {
      method_->since_revision = value;
      return *this;
   }

   method_builder& deprecated(std::string reason) {
      method_->deprecated = true;
      method_->deprecation_reason = std::move(reason);
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

template <typename Interface> contract_builder<Interface> define(descriptor value) {
   value.interface_type = typeid(Interface);
   return contract_builder<Interface>{std::move(value)};
}

} // namespace fcl::api
