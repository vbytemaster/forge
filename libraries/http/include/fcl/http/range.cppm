module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module fcl.http.range;

export namespace fcl::http {

struct byte_range {
   std::uint64_t first = 0;
   std::uint64_t last = 0;
};

struct range_request {
   bool present = false;
   bool satisfiable = false;
   byte_range bytes;
};

struct range_response {
   bool partial = false;
   bool satisfiable = true;
   byte_range bytes;
   std::uint64_t total_size = 0;
   std::string content_range;
};

[[nodiscard]] range_request parse_range_header(std::string_view value, std::uint64_t size);
[[nodiscard]] range_response resolve_range(std::optional<std::string_view> header, std::uint64_t size);

} // namespace fcl::http
