module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

export module forge.plugins.db.rocksdb.plugin;

export import forge.plugins.db.rocksdb.api;

import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.api.binding;
import forge.config.component;

namespace forge::plugins::db::rocksdb {
namespace detail {
struct lifecycle;
} // namespace detail
} // namespace forge::plugins::db::rocksdb

export namespace forge::plugins::db::rocksdb {

class plugin final : public forge::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] forge::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<forge::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(forge::config::component_view view) override;
   boost::asio::awaitable<void> provide(forge::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   friend struct detail::lifecycle;
   struct impl;
   class api_impl;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] forge::app::plugin_descriptor descriptor();

} // namespace forge::plugins::db::rocksdb
