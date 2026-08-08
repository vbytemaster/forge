module;

#include <forge/exceptions/policy.hpp>

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

module forge.chain.protocol.state_query;

namespace forge::chain::protocol {

table_index table_index::from_string(std::string_view value) {
   const auto separator = value.find(':');
   if (separator == std::string_view::npos || separator == 0U || separator + 1U >= value.size()) {
      FORGE_POLICY_THROW_STANDARD(std::invalid_argument, "table index must use <kind>:<position> syntax");
   }

   auto parsed_kind = table_index_kind{};
   const auto kind = value.substr(0U, separator);
   if (kind == "primary") {
      parsed_kind = table_index_kind::primary;
   } else if (kind == "secondary-u64") {
      parsed_kind = table_index_kind::secondary_u64;
   } else if (kind == "secondary-u128") {
      parsed_kind = table_index_kind::secondary_u128;
   } else if (kind == "secondary-u256") {
      parsed_kind = table_index_kind::secondary_u256;
   } else if (kind == "secondary-f64") {
      parsed_kind = table_index_kind::secondary_f64;
   } else if (kind == "secondary-f128") {
      parsed_kind = table_index_kind::secondary_f128;
   } else {
      FORGE_POLICY_THROW_STANDARD(std::invalid_argument, "table index kind is invalid");
   }

   auto parsed_position = std::uint16_t{};
   const auto position = value.substr(separator + 1U);
   const auto [end, error] = std::from_chars(position.data(), position.data() + position.size(), parsed_position);
   if (error != std::errc{} || end != position.data() + position.size() || parsed_position > UINT8_MAX) {
      FORGE_POLICY_THROW_STANDARD(std::invalid_argument, "table index position is invalid");
   }
   return table_index{.kind = parsed_kind, .position = static_cast<std::uint8_t>(parsed_position)};
}

std::string table_index::to_string() const {
   auto kind_name = std::string_view{};
   switch (kind) {
   case table_index_kind::primary:
      kind_name = "primary";
      break;
   case table_index_kind::secondary_u64:
      kind_name = "secondary-u64";
      break;
   case table_index_kind::secondary_u128:
      kind_name = "secondary-u128";
      break;
   case table_index_kind::secondary_u256:
      kind_name = "secondary-u256";
      break;
   case table_index_kind::secondary_f64:
      kind_name = "secondary-f64";
      break;
   case table_index_kind::secondary_f128:
      kind_name = "secondary-f128";
      break;
   default:
      FORGE_POLICY_THROW_STANDARD(std::invalid_argument, "table index kind is invalid");
   }
   return std::string{kind_name} + ':' + std::to_string(position);
}

} // namespace forge::chain::protocol
