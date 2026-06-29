#pragma once

namespace forge::plugins::db::rocksdb {

enum class phase : std::uint8_t {
   registered,
   configured,
   initialized,
   started,
   stopping,
   stopped,
};

struct plugin::impl {
   mutable std::mutex mutex;
   config settings;
   std::shared_ptr<forge::rocksdb::store> store;
   forge::asio::task_scheduler* scheduler = nullptr;
   std::atomic<phase> current = phase::registered;

   void configure(config value);
   void set_scheduler(forge::asio::task_scheduler& value);
   void open();
   void request_stop() noexcept;
   void close();

   [[nodiscard]] std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task_scheduler*> require_running() const;
};

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<impl> owner);

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, std::vector<std::byte> key, read_options options) override;
   boost::asio::awaitable<void>
   put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value, write_options options) override;
   boost::asio::awaitable<void> erase(family column_family, std::vector<std::byte> key, write_options options) override;
   boost::asio::awaitable<void> write(std::vector<operation> operations, write_options options) override;
   boost::asio::awaitable<std::vector<entry>>
   scan(family column_family, std::vector<std::byte> prefix, read_options options) override;
   boost::asio::awaitable<scan_result> scan_page(family column_family, scan_request request) override;
   boost::asio::awaitable<std::shared_ptr<transaction>> begin(write_options options) override;
   boost::asio::awaitable<void> flush_wal(bool sync) override;

 private:
   std::shared_ptr<impl> owner_;
};

namespace detail {

struct lifecycle {
[[nodiscard]] static std::shared_ptr<plugin::impl> make_impl();
[[nodiscard]] static std::optional<forge::config::component_descriptor> describe_config(const std::shared_ptr<plugin::impl>& impl);
static boost::asio::awaitable<void> configure(const std::shared_ptr<plugin::impl>& impl, forge::config::component_view view);
static boost::asio::awaitable<void> provide(const std::shared_ptr<plugin::impl>& impl, forge::api::provider& provider);
static boost::asio::awaitable<void> initialize(const std::shared_ptr<plugin::impl>& impl, forge::app::plugin_context& context);
static boost::asio::awaitable<void> startup(const std::shared_ptr<plugin::impl>& impl);
static void request_stop(const std::shared_ptr<plugin::impl>& impl) noexcept;
static boost::asio::awaitable<void> shutdown(const std::shared_ptr<plugin::impl>& impl);
};

} // namespace detail

namespace detail {

template <typename T> struct scheduled_result {
   std::mutex mutex;
   std::optional<T> value;
   std::exception_ptr error;
};

template <> struct scheduled_result<void> {
   std::mutex mutex;
   std::exception_ptr error;
};

template <typename Fn>
boost::asio::awaitable<std::invoke_result_t<Fn&>> run_scheduled(
   forge::asio::task_scheduler& scheduler,
   std::string name,
   Fn fn) {
   using result_type = std::invoke_result_t<Fn&>;
   auto state = std::make_shared<scheduled_result<result_type>>();
   auto handle = scheduler.submit(
      forge::asio::task{
         .priority = forge::asio::priority{},
         .name = std::move(name),
         .work =
            [state, fn = std::move(fn)]() mutable {
               try {
                  if constexpr (std::is_void_v<result_type>) {
                     fn();
                  } else {
                     auto value = fn();
                     const auto lock = std::scoped_lock{state->mutex};
                     state->value = std::move(value);
                  }
               } catch (...) {
                  const auto lock = std::scoped_lock{state->mutex};
                  state->error = std::current_exception();
               }
            },
      });

   co_await handle.wait();

   auto lock = std::scoped_lock{state->mutex};
   if (state->error) {
      std::rethrow_exception(state->error);
   }

   if constexpr (std::is_void_v<result_type>) {
      co_return;
   } else {
      co_return std::move(*state->value);
   }
}

} // namespace detail


} // namespace forge::plugins::db::rocksdb
