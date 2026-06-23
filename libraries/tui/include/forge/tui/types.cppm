module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module forge.tui.types;

export namespace forge::tui {

enum class severity : std::uint8_t {
   info,
   warning,
   error,
   critical,
};

enum class status : std::uint8_t {
   ok,
   degraded,
   blocked,
   offline,
   unknown,
};

enum class action_state : std::uint8_t {
   enabled,
   disabled,
   dangerous,
   loading,
};

enum class color_mode : std::uint8_t {
   unknown,
   monochrome,
   ansi_256,
   truecolor,
};

struct theme {
   std::string title = "cyan";
   std::string accent = "blue";
   std::string warning = "yellow";
   std::string error = "red";
   std::string muted = "gray";
};

struct status_badge_model {
   status value = status::unknown;
   std::string label;
};

struct key_value_item {
   std::string key;
   std::string value;
   bool sensitive = false;
};

struct table_column {
   std::string title;
   std::size_t width = 0;
};

struct table_row {
   std::vector<std::string> cells;
   status row_status = status::unknown;
};

struct table_model {
   std::vector<table_column> columns;
   std::vector<table_row> rows;
   std::string empty_text = "empty";
   bool loading = false;
   std::string error;
};

struct form_field {
   std::string name;
   std::string label;
   std::string value;
   bool required = false;
   bool sensitive = false;
   std::string error;
};

struct form_model {
   std::vector<form_field> fields;
};

struct validation_result {
   bool ok = true;
   std::vector<std::string> errors;
};

struct navigation_item {
   std::string id;
   std::string label;
   action_state state = action_state::enabled;
};

struct navigation_model {
   std::vector<navigation_item> items;
   std::size_t selected = 0;
};

struct event_item {
   severity level = severity::info;
   std::string topic;
   std::string message;
};

struct event_log_model {
   std::vector<event_item> events;
   std::size_t max_items = 10;
};

struct terminal_capabilities {
   bool available = false;
   color_mode colors = color_mode::unknown;
   bool unicode = false;
   std::uint32_t width = 0;
   std::uint32_t height = 0;
   std::string degraded_reason;
};

struct shell_model {
   std::string title = "FORGE Control";
   std::string profile;
   std::string endpoint;
   std::string actor;
   navigation_model navigation;
   std::vector<std::string> content_lines;
   event_log_model events;
   std::string lifecycle_state;
   std::string last_error;
};

struct input_event {
   enum class kind : std::uint8_t {
      none,
      quit,
      back,
      select,
      up,
      down,
   };

   kind value = kind::none;
};

using input_source = std::function<std::optional<input_event>()>;

} // namespace forge::tui
