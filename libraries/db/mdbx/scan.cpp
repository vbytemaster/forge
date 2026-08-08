module;

#include <mdbx.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

module forge.db.mdbx.driver;

import :error;
import forge.db.core.record;

#include "details/scan.hxx"

namespace forge::db::mdbx::detail {
namespace {

MDBX_val native_value(const std::vector<std::byte>& bytes) noexcept {
   return MDBX_val{.iov_base = const_cast<std::byte*>(bytes.data()),
                   .iov_len = bytes.size()};
}

std::vector<std::byte> copy_value(const MDBX_val& value) {
   const auto* begin = static_cast<const std::byte*>(value.iov_base);
   return std::vector<std::byte>{begin, begin + value.iov_len};
}

bool starts_with(const std::vector<std::byte>& value,
                 const std::vector<std::byte>& prefix) noexcept {
   return value.size() >= prefix.size() &&
          std::equal(prefix.begin(), prefix.end(), value.begin());
}

const std::vector<std::byte>& lower_bound(
   const forge::db::core::record_range& range) noexcept {
   return range.begin.empty() ? range.prefix.bytes() : range.begin.bytes();
}

bool within_range(const std::vector<std::byte>& key,
                  const forge::db::core::record_range& range) noexcept {
   if (!starts_with(key, range.prefix.bytes())) {
      return false;
   }
   if (key < range.begin.bytes()) {
      return false;
   }
   return !range.has_end || key < range.end.bytes();
}

int seek(MDBX_cursor* cursor,
         const forge::db::core::record_range& range,
         const forge::db::core::page_request& request,
         MDBX_val& key,
         MDBX_val& value) {
   if (request.after.has_value()) {
      const auto& boundary = request.after->boundary.bytes();
      const auto& lower = lower_bound(range);
      const auto& start = boundary < lower ? lower : boundary;
      key = native_value(start);
      auto code = mdbx_cursor_get(cursor, &key, &value, MDBX_SET_RANGE);
      if (code != MDBX_SUCCESS) {
         return code;
      }
      if (copy_value(key) == boundary) {
         code = mdbx_cursor_get(cursor, &key, &value, MDBX_NEXT);
      }
      return code;
   }

   const auto& lower = lower_bound(range);
   if (lower.empty()) {
      return mdbx_cursor_get(cursor, &key, &value, MDBX_FIRST);
   }
   key = native_value(lower);
   return mdbx_cursor_get(cursor, &key, &value, MDBX_SET_RANGE);
}

} // namespace

forge::db::core::record_page scan_records(
   MDBX_txn* transaction,
   MDBX_dbi family,
   const forge::db::core::record_range& range,
   const forge::db::core::page_request& request) {
   forge::db::core::validate_page_request(request);

   auto* cursor = static_cast<MDBX_cursor*>(nullptr);
   require_mdbx_success(mdbx_cursor_open(transaction, family, &cursor),
                        "mdbx_cursor_open");

   try {
      auto result = forge::db::core::record_page{};
      auto key = MDBX_val{};
      auto value = MDBX_val{};
      auto code = seek(cursor, range, request, key, value);
      while (code == MDBX_SUCCESS) {
         auto key_bytes = copy_value(key);
         if (!within_range(key_bytes, range)) {
            break;
         }
         if (result.entries.size() == request.limit) {
            result.next = forge::db::core::cursor{
               .boundary = result.entries.back().key,
            };
            break;
         }

         result.entries.push_back(forge::db::core::record_entry{
            .key = forge::db::core::record_key{std::move(key_bytes)},
            .value = copy_value(value),
         });
         code = mdbx_cursor_get(cursor, &key, &value, MDBX_NEXT);
      }

      if (!mdbx_not_found(code)) {
         require_mdbx_success(code, "mdbx_cursor_get");
      }
      require_mdbx_success(mdbx_cursor_close2(cursor), "mdbx_cursor_close2");
      return result;
   } catch (...) {
      static_cast<void>(mdbx_cursor_close2(cursor));
      throw;
   }
}

} // namespace forge::db::mdbx::detail
