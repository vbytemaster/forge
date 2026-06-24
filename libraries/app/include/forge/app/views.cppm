module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module forge.app.views;

export namespace forge::app {

enum class view_kind : std::uint8_t {
   summary,
   counters,
   table,
   log,
   tree,
   actions,
};

enum class view_severity : std::uint8_t {
   debug,
   info,
   warning,
   error,
   critical,
};

struct view_descriptor {
   std::string id;
   std::string title;
   std::string category;
   view_kind kind = view_kind::summary;
};

struct view_filter {
   std::string field;
   std::string value;
};

struct view_sort {
   std::string field;
   bool descending = false;
};

struct view_query {
   std::string cursor;
   std::uint64_t limit = 100;
   std::vector<view_filter> filters;
   std::vector<view_sort> sort;
};

struct view_counter {
   std::string name;
   std::string value;
   view_severity severity = view_severity::info;
};

struct view_table_column {
   std::string id;
   std::string title;
};

struct view_table_row {
   std::vector<std::string> cells;
   view_severity severity = view_severity::info;
};

struct view_log_item {
   std::uint64_t id = 0;
   view_severity severity = view_severity::info;
   std::string topic;
   std::string message;
};

struct view_action {
   std::string id;
   std::string label;
   view_severity severity = view_severity::info;
   bool enabled = true;
};

struct view_page {
   std::vector<view_table_row> rows;
   std::optional<std::string> next_cursor;
   std::optional<std::uint64_t> total_estimate;
};

struct view_snapshot {
   view_descriptor descriptor;
   std::vector<view_counter> counters;
   std::vector<view_table_column> columns;
   view_page page;
   std::vector<view_log_item> log;
   std::vector<view_action> actions;
   std::string error;
};

class view_source {
 public:
   virtual ~view_source() = default;
   [[nodiscard]] virtual boost::asio::awaitable<view_snapshot> snapshot(view_query query) = 0;
};

class view_registration {
 public:
   view_registration();
   ~view_registration();

   view_registration(view_registration&& other) noexcept;
   view_registration& operator=(view_registration&& other) noexcept;

   view_registration(const view_registration&) = delete;
   view_registration& operator=(const view_registration&) = delete;

   void unregister() noexcept;
   [[nodiscard]] bool active() const noexcept;

 private:
   struct state;
   std::shared_ptr<state> state_;

   explicit view_registration(std::shared_ptr<state> state);

   friend class view_registry;
};

class view_registry {
 public:
   explicit view_registry(std::uint64_t default_limit = 100);
   ~view_registry();

   view_registry(const view_registry&) = delete;
   view_registry& operator=(const view_registry&) = delete;

   [[nodiscard]] view_registration register_source(view_descriptor descriptor, std::shared_ptr<view_source> source);
   void unregister_source(const std::string& id) noexcept;
   [[nodiscard]] std::vector<view_descriptor> descriptors() const;
   [[nodiscard]] boost::asio::awaitable<view_snapshot> snapshot(std::string id, view_query query = {});
   [[nodiscard]] boost::asio::awaitable<std::vector<view_snapshot>> snapshots(view_query query = {});
   [[nodiscard]] std::uint64_t default_limit() const noexcept;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;

   friend class view_registration;
};

} // namespace forge::app
