module;

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module forge.config.value;

export namespace forge::config {

struct value {
   using array_type = std::vector<value>;
   using object_type = std::map<std::string, value>;
   using storage_type =
       std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, array_type, object_type>;

   storage_type storage;

   value() = default;
   value(std::nullptr_t) : storage{std::monostate{}} {}
   value(bool input) : storage{input} {}
   value(const char* input) : storage{std::string{input}} {}
   value(std::string input) : storage{std::move(input)} {}
   value(array_type input) : storage{std::move(input)} {}
   value(object_type input) : storage{std::move(input)} {}

   template <typename T>
      requires(std::signed_integral<T> && !std::same_as<std::remove_cvref_t<T>, bool>)
   value(T input) : storage{static_cast<std::int64_t>(input)} {}

   template <typename T>
      requires(std::unsigned_integral<T> && !std::same_as<std::remove_cvref_t<T>, bool>)
   value(T input) : storage{static_cast<std::uint64_t>(input)} {}

   template <typename T>
      requires(std::floating_point<T>)
   value(T input) : storage{static_cast<double>(input)} {}

   [[nodiscard]] bool is_null() const noexcept {
      return std::holds_alternative<std::monostate>(storage);
   }
   [[nodiscard]] bool is_object() const noexcept {
      return std::holds_alternative<object_type>(storage);
   }
   [[nodiscard]] bool is_array() const noexcept {
      return std::holds_alternative<array_type>(storage);
   }

   [[nodiscard]] const object_type* as_object() const noexcept {
      return std::get_if<object_type>(&storage);
   }
   [[nodiscard]] object_type* as_object() noexcept {
      return std::get_if<object_type>(&storage);
   }
   [[nodiscard]] const array_type* as_array() const noexcept {
      return std::get_if<array_type>(&storage);
   }
};

} // namespace forge::config
