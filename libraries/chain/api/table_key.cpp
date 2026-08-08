module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module forge.chain.api.table_key;

import forge.chain.api.exceptions;

namespace forge::chain::api {
namespace {

template <typename Integer> protocol::bytes encode_unsigned(Integer value, std::size_t size) {
   auto result = protocol::bytes(size, 0U);
   for (auto offset = std::size_t{}; offset < size; ++offset) {
      result[size - offset - 1U] = static_cast<std::uint8_t>(value & 0xffU);
      value >>= 8U;
   }
   return result;
}

[[noreturn]] void invalid_key(const char* message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_request, message);
}

} // namespace

protocol::bytes encode_table_key(std::uint64_t value) {
   return encode_unsigned(value, sizeof(value));
}

protocol::bytes encode_table_key(protocol::uint128_t value) {
   return encode_unsigned(value, sizeof(value));
}

protocol::bytes encode_table_key(const protocol::key256& value) {
   const auto bytes = value.extract_as_byte_array();
   return {bytes.begin(), bytes.end()};
}

protocol::bytes encode_table_key(double value) {
   if (std::isnan(value)) {
      invalid_key("floating table key cannot be NaN");
   }
   if (value == 0.0) {
      value = 0.0;
   }
   constexpr auto sign = std::uint64_t{1U} << 63U;
   const auto bits = std::bit_cast<std::uint64_t>(value);
   return encode_unsigned((bits & sign) != 0U ? ~bits : bits ^ sign, sizeof(bits));
}

protocol::bytes encode_table_key(std::span<const std::uint8_t, 16> value) {
   const auto exponent_all_ones = (value[0] & 0x7fU) == 0x7fU && value[1] == 0xffU;
   const auto fraction_nonzero = std::ranges::any_of(value.subspan(2U), [](auto byte) { return byte != 0U; });
   if (exponent_all_ones && fraction_nonzero) {
      invalid_key("binary128 table key cannot be NaN");
   }

   auto result = protocol::bytes{value.begin(), value.end()};
   if ((result.front() & 0x7fU) == 0U &&
       std::ranges::all_of(std::span{result}.subspan(1U), [](auto byte) { return byte == 0U; })) {
      result.front() = 0U;
   }
   if ((result.front() & 0x80U) != 0U) {
      std::ranges::transform(result, result.begin(), [](auto byte) { return static_cast<std::uint8_t>(~byte); });
   } else {
      result.front() ^= 0x80U;
   }
   return result;
}

void validate_table_index(protocol::table_index index) {
   switch (index.kind) {
   case protocol::table_index_kind::primary:
      if (index.position != 0U) {
         invalid_key("primary table index position must be zero");
      }
      return;
   case protocol::table_index_kind::secondary_u64:
   case protocol::table_index_kind::secondary_u128:
   case protocol::table_index_kind::secondary_u256:
   case protocol::table_index_kind::secondary_f64:
   case protocol::table_index_kind::secondary_f128:
      break;
   default:
      invalid_key("table index kind is invalid");
   }
   if (index.position >= 16U) {
      invalid_key("secondary table index position must be less than 16");
   }
}

void validate_table_key(protocol::table_index_kind kind, std::span<const std::uint8_t> value) {
   auto expected = std::size_t{};
   switch (kind) {
   case protocol::table_index_kind::primary:
   case protocol::table_index_kind::secondary_u64:
   case protocol::table_index_kind::secondary_f64:
      expected = 8U;
      break;
   case protocol::table_index_kind::secondary_u128:
   case protocol::table_index_kind::secondary_f128:
      expected = 16U;
      break;
   case protocol::table_index_kind::secondary_u256:
      expected = 32U;
      break;
   default:
      invalid_key("table index kind is invalid");
   }
   if (value.size() != expected) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "table key has the wrong canonical width",
                            forge::exceptions::ctx("expected", expected),
                            forge::exceptions::ctx("actual", value.size()));
   }
}

void validate_table_rows_request(const protocol::table_rows_request& request) {
   validate_table_index(request.index);
   if (request.lower_bound) {
      validate_table_key(request.index.kind, *request.lower_bound);
   }
   if (request.upper_bound) {
      validate_table_key(request.index.kind, *request.upper_bound);
   }
   if (request.lower_bound && request.upper_bound &&
       std::lexicographical_compare(request.upper_bound->begin(), request.upper_bound->end(),
                                    request.lower_bound->begin(), request.lower_bound->end())) {
      invalid_key("table lower bound must not exceed its upper bound");
   }
}

} // namespace forge::chain::api
