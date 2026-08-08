#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <cstdint>
#include <optional>

import forge.db.authenticated.proof;
import forge.db.authenticated.store;
import forge.db.authenticated.transaction;
import forge.db.authenticated.types;

int main() {
   static_assert(std::same_as<forge::db::authenticated::version_id_t, std::uint64_t>);
   static_assert(requires(const forge::db::authenticated::store& value) {
      { value.earliest() } -> std::same_as<boost::asio::awaitable<std::optional<forge::db::authenticated::root>>>;
   });

   const auto options = forge::db::authenticated::prune_options{
       .max_versions = 32,
       .max_garbage_records = 1'024,
   };
   const auto request = forge::db::authenticated::range_request{
       .limit = 128,
       .reverse = true,
   };
   return options.max_versions == 32 && request.limit == 128 && request.reverse ? 0 : 1;
}
