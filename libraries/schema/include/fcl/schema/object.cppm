module;

#include <any>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

export module fcl.schema.object;

import fcl.schema.diagnostic;
import fcl.schema.value_kind;

export namespace fcl::schema {

template <typename T> struct member_pointer_traits;

template <typename Object, typename Member> struct member_pointer_traits<Member Object::*> {
   using object_type = Object;
   using member_type = Member;
};

template <typename T> [[nodiscard]] T cast_any_to(const std::any& value) {
   using clean_type = std::remove_cvref_t<T>;
   if (value.type() == typeid(clean_type)) {
      return std::any_cast<clean_type>(value);
   }
   if constexpr (std::same_as<clean_type, std::string>) {
      if (value.type() == typeid(const char*)) {
         return std::string{std::any_cast<const char*>(value)};
      }
      if (value.type() == typeid(char*)) {
         return std::string{std::any_cast<char*>(value)};
      }
   } else if constexpr (std::integral<clean_type> && !std::same_as<clean_type, bool>) {
      if (value.type() == typeid(int)) {
         return static_cast<clean_type>(std::any_cast<int>(value));
      }
      if (value.type() == typeid(unsigned int)) {
         return static_cast<clean_type>(std::any_cast<unsigned int>(value));
      }
      if (value.type() == typeid(long)) {
         return static_cast<clean_type>(std::any_cast<long>(value));
      }
      if (value.type() == typeid(unsigned long)) {
         return static_cast<clean_type>(std::any_cast<unsigned long>(value));
      }
      if (value.type() == typeid(long long)) {
         return static_cast<clean_type>(std::any_cast<long long>(value));
      }
      if (value.type() == typeid(unsigned long long)) {
         return static_cast<clean_type>(std::any_cast<unsigned long long>(value));
      }
      if (value.type() == typeid(std::int64_t)) {
         return static_cast<clean_type>(std::any_cast<std::int64_t>(value));
      }
      if (value.type() == typeid(std::uint64_t)) {
         return static_cast<clean_type>(std::any_cast<std::uint64_t>(value));
      }
   } else if constexpr (std::floating_point<clean_type>) {
      if (value.type() == typeid(float)) {
         return static_cast<clean_type>(std::any_cast<float>(value));
      }
      if (value.type() == typeid(double)) {
         return static_cast<clean_type>(std::any_cast<double>(value));
      }
      if (value.type() == typeid(long double)) {
         return static_cast<clean_type>(std::any_cast<long double>(value));
      }
   }
   return std::any_cast<clean_type>(value);
}

template <typename T> struct field_rule {
   std::string name;
   std::vector<std::string> aliases;
   value_kind kind = value_kind::string;
   std::type_index type = std::type_index{typeid(void)};
   bool required = false;
   bool secret = false;
   bool deprecated = false;
   std::string deprecated_message;
   std::string description;
   bool has_default = false;
   std::any default_value;
   std::optional<long double> minimum;
   std::optional<long double> maximum;
   std::function<void(T&)> apply_default;
   std::function<void(T&, const std::any&)> assign_any;
   std::function<std::any(const T&)> read_any;
};

template <typename T> class field_builder;

template <typename T> class object_schema {
 public:
   object_schema() : fields_{std::make_shared<std::vector<field_rule<T>>>()} {}

   template <auto Member> field_builder<T> field(std::string name) {
      using pointer_traits = member_pointer_traits<decltype(Member)>;
      using object_type = typename pointer_traits::object_type;
      using member_type = std::remove_cvref_t<typename pointer_traits::member_type>;
      static_assert(std::same_as<object_type, T>, "schema field member must belong to schema object type");

      auto rule = field_rule<T>{};
      rule.name = std::move(name);
      rule.kind = member_kind<member_type>::value;
      rule.type = std::type_index{typeid(member_type)};
      rule.assign_any = [](T& object, const std::any& value) { object.*Member = cast_any_to<member_type>(value); };
      rule.read_any = [](const T& object) -> std::any { return object.*Member; };
      rule.apply_default = [state = fields_, index = fields_->size()](T& object) {
         const auto& self = (*state)[index];
         if (self.has_default) {
            self.assign_any(object, self.default_value);
         }
      };

      fields_->push_back(std::move(rule));
      return field_builder<T>{*this, fields_->size() - 1};
   }

   [[nodiscard]] const std::vector<field_rule<T>>& fields() const noexcept {
      return *fields_;
   }

   void apply_defaults(T& object) const {
      for (const auto& field : *fields_) {
         field.apply_default(object);
      }
   }

   [[nodiscard]] std::vector<diagnostic> validate(const T& object, std::string_view base_path = {}) const {
      auto result = std::vector<diagnostic>{};
      for (const auto& field : *fields_) {
         if (!field.minimum && !field.maximum) {
            continue;
         }

         auto numeric = std::optional<long double>{};
         try {
            switch (field.kind) {
            case value_kind::signed_integer:
               numeric = static_cast<long double>(std::any_cast<std::int64_t>(coerce_signed(field.read_any(object))));
               break;
            case value_kind::unsigned_integer:
               numeric =
                   static_cast<long double>(std::any_cast<std::uint64_t>(coerce_unsigned(field.read_any(object))));
               break;
            case value_kind::floating:
               numeric = std::any_cast<long double>(coerce_floating(field.read_any(object)));
               break;
            default:
               break;
            }
         } catch (...) {
            result.push_back(
                make_error(base_path, field.name, "schema.type", "value cannot be inspected for range validation"));
            continue;
         }

         if (!numeric) {
            continue;
         }
         if (field.minimum && *numeric < *field.minimum) {
            result.push_back(make_error(base_path, field.name, "schema.range", "value is below the allowed minimum"));
         }
         if (field.maximum && *numeric > *field.maximum) {
            result.push_back(make_error(base_path, field.name, "schema.range", "value is above the allowed maximum"));
         }
      }
      return result;
   }

 private:
   friend class field_builder<T>;

   field_rule<T>& field_at(std::size_t index) {
      return (*fields_)[index];
   }

   static std::any coerce_signed(const std::any& value) {
      if (value.type() == typeid(std::int64_t)) {
         return value;
      }
      if (value.type() == typeid(int)) {
         return static_cast<std::int64_t>(std::any_cast<int>(value));
      }
      if (value.type() == typeid(short)) {
         return static_cast<std::int64_t>(std::any_cast<short>(value));
      }
      if (value.type() == typeid(long)) {
         return static_cast<std::int64_t>(std::any_cast<long>(value));
      }
      if (value.type() == typeid(long long)) {
         return static_cast<std::int64_t>(std::any_cast<long long>(value));
      }
      return value;
   }

   static std::any coerce_unsigned(const std::any& value) {
      if (value.type() == typeid(std::uint64_t)) {
         return value;
      }
      if (value.type() == typeid(unsigned int)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned int>(value));
      }
      if (value.type() == typeid(unsigned short)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned short>(value));
      }
      if (value.type() == typeid(unsigned long)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned long>(value));
      }
      if (value.type() == typeid(unsigned long long)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned long long>(value));
      }
      return value;
   }

   static std::any coerce_floating(const std::any& value) {
      if (value.type() == typeid(long double)) {
         return value;
      }
      if (value.type() == typeid(double)) {
         return static_cast<long double>(std::any_cast<double>(value));
      }
      if (value.type() == typeid(float)) {
         return static_cast<long double>(std::any_cast<float>(value));
      }
      return value;
   }

   static diagnostic make_error(std::string_view base_path, const std::string& field, std::string code,
                                std::string message) {
      auto path = std::string{base_path};
      if (!path.empty()) {
         path += ".";
      }
      path += field;
      return diagnostic{
          .path = std::move(path), .code = std::move(code), .level = severity::error, .message = std::move(message)};
   }

   std::shared_ptr<std::vector<field_rule<T>>> fields_;
};

template <typename T> class field_builder {
 public:
   field_builder(object_schema<T> schema, std::size_t index) : schema_{std::move(schema)}, index_{index} {}

   field_builder& required() {
      current().required = true;
      return *this;
   }

   template <typename Value> field_builder& default_value(Value&& value) {
      current().has_default = true;
      current().default_value = std::forward<Value>(value);
      return *this;
   }

   template <typename Min, typename Max> field_builder& range(Min min, Max max) {
      current().minimum = static_cast<long double>(min);
      current().maximum = static_cast<long double>(max);
      return *this;
   }

   field_builder& secret() {
      current().secret = true;
      return *this;
   }

   field_builder& deprecated(std::string message) {
      current().deprecated = true;
      current().deprecated_message = std::move(message);
      return *this;
   }

   field_builder& description(std::string text) {
      current().description = std::move(text);
      return *this;
   }

   field_builder& alias(std::string name) {
      current().aliases.push_back(std::move(name));
      return *this;
   }

   template <auto Member> field_builder field(std::string name) {
      return schema_.template field<Member>(std::move(name));
   }

   [[nodiscard]] operator object_schema<T>() const {
      return schema_;
   }

 private:
   field_rule<T>& current() {
      return schema_.field_at(index_);
   }

   object_schema<T> schema_;
   std::size_t index_ = 0;
};

template <typename T> [[nodiscard]] object_schema<T> object() {
   return object_schema<T>{};
}

template <typename T> struct rules {
   [[nodiscard]] static object_schema<T> define() {
      return object<T>();
   }
};

} // namespace fcl::schema
