module;

#include <fcl/exceptions/macros.hpp>

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

module fcl.quic.endpoint;

namespace fcl::quic {
namespace {

constexpr auto scheme = std::string_view{"quic://"};

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept {
   return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

} // namespace

std::string endpoint::authority() const {
   return host + ":" + std::to_string(port);
}

endpoint parse_endpoint(std::string_view value) {
   if (!starts_with(value, scheme)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "QUIC endpoint must use quic:// scheme");
   }

   value.remove_prefix(scheme.size());
   if (value.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "QUIC endpoint host is empty");
   }

   auto host_value = std::string_view{};
   auto port_value = std::string_view{};

   if (value.front() == '[') {
      const auto end = value.find(']');
      if (end == std::string_view::npos || end + 2 > value.size() || value[end + 1] != ':') {
         FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "invalid bracketed QUIC endpoint");
      }
      host_value = value.substr(1, end - 1);
      port_value = value.substr(end + 2);
   } else {
      const auto separator = value.rfind(':');
      if (separator == std::string_view::npos) {
         FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "QUIC endpoint port is missing");
      }
      host_value = value.substr(0, separator);
      port_value = value.substr(separator + 1);
   }

   if (host_value.empty() || port_value.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "QUIC endpoint host or port is empty");
   }

   auto parsed_port = unsigned{};
   const auto* first = port_value.data();
   const auto* last = port_value.data() + port_value.size();
   const auto result = std::from_chars(first, last, parsed_port);
   if (result.ec != std::errc{} || result.ptr != last || parsed_port > std::numeric_limits<std::uint16_t>::max()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, "QUIC endpoint port is invalid");
   }

   return endpoint{.host = std::string{host_value}, .port = static_cast<std::uint16_t>(parsed_port)};
}

} // namespace fcl::quic
