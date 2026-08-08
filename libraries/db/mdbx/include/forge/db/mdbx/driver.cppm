module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module forge.db.mdbx.driver;

import forge.asio.affine;
import forge.db.core.driver;

export import forge.db.mdbx.exceptions;

namespace forge::db::mdbx::detail {
class driver_impl;
}

export namespace forge::db::mdbx {

enum class durability {
   durable_sync,
   safe_nosync,
};

struct geometry {
   std::optional<std::uint64_t> lower_size;
   std::optional<std::uint64_t> current_size;
   std::optional<std::uint64_t> upper_size;
   std::optional<std::uint64_t> growth_step;
   std::optional<std::uint64_t> shrink_threshold;
   std::optional<std::uint32_t> page_size;
};

struct config {
   std::string path;
   std::vector<std::string> families{"default"};
   durability durability_mode = durability::durable_sync;
   geometry map;
   std::size_t max_readers = 128;
   bool create_if_missing = true;
   bool create_missing_families = true;
};

class driver final : public forge::db::core::driver {
 public:
   static boost::asio::awaitable<std::shared_ptr<driver>>
   open(config value, forge::asio::affine::executor executor);

   static boost::asio::awaitable<std::shared_ptr<driver>>
   open(config value, forge::asio::affine::lane::options lane_options);

   ~driver() override;

   boost::asio::awaitable<void> async_flush(bool sync) override;

 private:
   explicit driver(std::shared_ptr<detail::driver_impl> impl);

   static boost::asio::awaitable<std::shared_ptr<driver>>
   open_managed(config value,
                forge::asio::affine::executor executor,
                std::shared_ptr<forge::asio::affine::lane> lane);

   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>>
   open_transaction() override;
   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>>
   open_snapshot() override;
   boost::asio::awaitable<void> create_checkpoint_impl(std::filesystem::path destination) override;
   boost::asio::awaitable<void> close_driver() override;

   std::shared_ptr<detail::driver_impl> impl_;
};

BOOST_DESCRIBE_ENUM(durability, durable_sync, safe_nosync)
BOOST_DESCRIBE_STRUCT(geometry, (),
                      (lower_size, current_size, upper_size, growth_step,
                       shrink_threshold, page_size))
BOOST_DESCRIBE_STRUCT(config, (),
                      (path, families, durability_mode, map, max_readers,
                       create_if_missing, create_missing_families))

} // namespace forge::db::mdbx
