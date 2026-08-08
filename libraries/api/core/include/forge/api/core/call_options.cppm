module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.api.core.call_options;

export import forge.api.core.stream_reader;

export namespace forge::api::core {

struct call_options {
   std::size_t max_item_bytes = 1024U * 1024U;
   std::size_t max_buffered_items = 16;
   std::size_t max_buffered_bytes = 1024U * 1024U;
   std::optional<std::chrono::steady_clock::time_point> deadline;
};

namespace detail {

class call_result {
 public:
   call_result() = default;

   template <typename T>
   [[nodiscard]] static call_result make(T&& value) {
      using value_type = std::remove_cvref_t<T>;
      return call_result{
         std::make_shared<value_type>(std::forward<T>(value)),
         typeid(value_type)};
   }

   template <typename T>
   [[nodiscard]] T take() {
      if (type_ != typeid(T) || !value_) {
         throw exceptions::protocol_error{"API call returned an unexpected result type"};
      }
      return std::move(*std::static_pointer_cast<T>(std::move(value_)));
   }

 private:
   call_result(std::shared_ptr<void> value, std::type_index type)
       : value_{std::move(value)}, type_{type} {}

   std::shared_ptr<void> value_;
   std::type_index type_ = typeid(void);
};

class call_operation {
 public:
   virtual ~call_operation() = default;

   virtual void start(boost::asio::awaitable<call_result> body) = 0;
   virtual void cancel() noexcept = 0;
   virtual boost::asio::awaitable<call_result> async_finish() = 0;
};

[[nodiscard]] std::shared_ptr<call_operation>
make_call_operation(
   boost::asio::any_io_executor executor,
   std::vector<std::shared_ptr<stream_endpoint>> endpoints,
   std::optional<std::chrono::steady_clock::time_point> deadline);

class call_factory {
 public:
   template <typename Call, typename... Args>
   [[nodiscard]] static Call make(Args&&... args) {
      return Call{std::forward<Args>(args)...};
   }
};

} // namespace detail

} // namespace forge::api::core
