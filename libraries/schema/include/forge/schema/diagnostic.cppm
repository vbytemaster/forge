module;

#include <cstddef>
#include <string>

export module forge.schema.diagnostic;

export namespace forge::schema {

enum class severity {
   info,
   warning,
   error,
};

struct diagnostic {
   std::string path;
   std::string code;
   severity level = severity::error;
   std::string message;
   std::size_t line = 0;
   std::size_t column = 0;
};

} // namespace forge::schema
