module;
#include <fcl/core/macros.hpp>
#include <string>

module fcl.raw.datastream;

NO_RETURN void fcl::detail::raise_datastream_range(char const* method, size_t len, int64_t over) {
   throw fcl::raw::exceptions::range_error{
      std::string(method) + " datastream of length " + std::to_string(len) + " over by " + std::to_string(over)};
}
