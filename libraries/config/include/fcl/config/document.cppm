module;

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <cstddef>

export module fcl.config.document;

import fcl.config.key_path;
import fcl.config.value;

export namespace fcl::config {

struct source_location {
   std::string source;
   std::size_t line = 0;
   std::size_t column = 0;
};

struct document {
   value::object_type root;
   std::map<std::string, source_location> locations;

   void set(std::string dotted_path, value input, source_location location = {});
   bool erase(std::string_view dotted_path);
   bool rename(std::string_view from_path, std::string_view to_path, bool overwrite = false);
   [[nodiscard]] const value* try_get(std::string_view dotted_path) const;
   [[nodiscard]] const value::object_type* object_at(std::string_view dotted_path) const;
};

[[nodiscard]] document merge(std::initializer_list<document> layers);
[[nodiscard]] document effective_document(std::initializer_list<document> layers);

} // namespace fcl::config
