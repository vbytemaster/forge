module;

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

module forge.tui.render;

import forge.tui.types;

namespace forge::tui {
namespace {

std::string lower(std::string_view value) {
   auto out = std::string{value};
   std::ranges::transform(out, out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
   return out;
}

bool looks_sensitive(std::string_view value) {
   const auto text = lower(value);
   return text.find("private_key") != std::string::npos || text.find("private-key") != std::string::npos ||
          text.find("private key") != std::string::npos || text.find("pvt_") != std::string::npos ||
          text.find("token") != std::string::npos || text.find("workspace_secret") != std::string::npos ||
          text.find("workspace-secret") != std::string::npos || text.find("workspace secret") != std::string::npos ||
          text.find("recovery") != std::string::npos || text.find("storage_handle") != std::string::npos ||
          text.find("storage-handle") != std::string::npos || text.find("passphrase") != std::string::npos ||
          text.find("storage handle") != std::string::npos || text.find("secret") != std::string::npos;
}

std::string join_cells(const std::vector<std::string>& cells, std::string_view separator) {
   auto out = std::ostringstream{};
   for (auto index = std::size_t{0}; index < cells.size(); ++index) {
      if (index != 0) {
         out << separator;
      }
      out << cells[index];
   }
   return out.str();
}

std::string redact_endpoint(std::string_view value) {
   auto endpoint = std::string{redact_text(value)};
   if (endpoint == "<redacted>") {
      return endpoint;
   }

   const auto scheme = endpoint.find("://");
   const auto authority_begin = scheme == std::string::npos ? std::size_t{0} : scheme + 3;
   const auto path_begin = endpoint.find_first_of("/?#", authority_begin);
   const auto authority_end = path_begin == std::string::npos ? endpoint.size() : path_begin;
   const auto at = endpoint.find('@', authority_begin);
   if (at != std::string::npos && at < authority_end) {
      endpoint.replace(authority_begin, at - authority_begin, "<redacted>");
   }

   const auto query = endpoint.find('?', authority_begin);
   if (query != std::string::npos) {
      const auto fragment = endpoint.find('#', query + 1);
      if (fragment == std::string::npos) {
         endpoint.replace(query + 1, std::string::npos, "<redacted>");
      } else {
         endpoint.replace(query + 1, fragment - query - 1, "<redacted>");
      }
   }
   return endpoint;
}

} // namespace

std::string to_string(severity value) {
   switch (value) {
   case severity::info:
      return "info";
   case severity::warning:
      return "warning";
   case severity::error:
      return "error";
   case severity::critical:
      return "critical";
   }
   return "unknown";
}

std::string to_string(status value) {
   switch (value) {
   case status::ok:
      return "ok";
   case status::degraded:
      return "degraded";
   case status::blocked:
      return "blocked";
   case status::offline:
      return "offline";
   case status::unknown:
      return "unknown";
   }
   return "unknown";
}

std::string to_string(action_state value) {
   switch (value) {
   case action_state::enabled:
      return "enabled";
   case action_state::disabled:
      return "disabled";
   case action_state::dangerous:
      return "dangerous";
   case action_state::loading:
      return "loading";
   }
   return "unknown";
}

std::string to_string(color_mode value) {
   switch (value) {
   case color_mode::unknown:
      return "unknown";
   case color_mode::monochrome:
      return "monochrome";
   case color_mode::ansi_256:
      return "ansi_256";
   case color_mode::truecolor:
      return "truecolor";
   }
   return "unknown";
}

std::string redact_text(std::string_view value) {
   if (value.empty()) {
      return {};
   }
   if (looks_sensitive(value)) {
      return "<redacted>";
   }
   return std::string{value};
}

std::vector<std::string> render_status_badge(const status_badge_model& model) {
   const auto label = model.label.empty() ? to_string(model.value) : model.label;
   return {"[" + to_string(model.value) + "] " + label};
}

std::vector<std::string> render_key_value_panel(const std::vector<key_value_item>& items) {
   auto lines = std::vector<std::string>{};
   lines.reserve(items.size());
   for (const auto& item : items) {
      const auto hide = item.sensitive || looks_sensitive(item.key) || looks_sensitive(item.value);
      auto value = hide ? std::string{"<redacted>"} : redact_text(item.value);
      lines.push_back(item.key + ": " + value);
   }
   return lines;
}

std::vector<std::string> render_table(const table_model& model) {
   if (!model.error.empty()) {
      return {"error: " + model.error};
   }
   if (model.loading) {
      return {"loading..."};
   }
   if (model.rows.empty()) {
      return {model.empty_text};
   }

   auto lines = std::vector<std::string>{};
   auto headers = std::vector<std::string>{};
   headers.reserve(model.columns.size());
   for (const auto& column : model.columns) {
      headers.push_back(column.title);
   }
   if (!headers.empty()) {
      lines.push_back(join_cells(headers, " | "));
      lines.push_back(std::string(lines.back().size(), '-'));
   }
   for (const auto& row : model.rows) {
      auto line = join_cells(row.cells, " | ");
      if (row.row_status != status::unknown) {
         line += " [" + to_string(row.row_status) + "]";
      }
      lines.push_back(std::move(line));
   }
   return lines;
}

std::vector<std::string> render_form(const form_model& model) {
   auto lines = std::vector<std::string>{};
   lines.reserve(model.fields.size());
   for (const auto& field : model.fields) {
      auto line = field.label + ": ";
      const auto hide = field.sensitive || looks_sensitive(field.name) || looks_sensitive(field.label) ||
                        looks_sensitive(field.value);
      line += hide ? "<redacted>" : redact_text(field.value);
      if (!field.error.empty()) {
         line += " (error: " + field.error + ")";
      }
      lines.push_back(std::move(line));
   }
   return lines;
}

std::vector<std::string> render_modal(std::string_view title, const std::vector<std::string>& lines) {
   auto out = std::vector<std::string>{};
   out.reserve(lines.size() + 2);
   out.push_back("== " + std::string{title} + " ==");
   out.insert(out.end(), lines.begin(), lines.end());
   out.push_back("[enter] confirm  [esc] cancel");
   return out;
}

std::vector<std::string> render_action_bar(const std::vector<navigation_item>& actions) {
   auto cells = std::vector<std::string>{};
   cells.reserve(actions.size());
   for (const auto& action : actions) {
      cells.push_back(action.label + " (" + to_string(action.state) + ")");
   }
   return {join_cells(cells, "  ")};
}

std::vector<std::string> render_event_log(const event_log_model& model) {
   auto lines = std::vector<std::string>{};
   const auto begin = model.events.size() > model.max_items ? model.events.size() - model.max_items : std::size_t{0};
   for (auto index = begin; index < model.events.size(); ++index) {
      const auto& event = model.events[index];
      lines.push_back("[" + to_string(event.level) + "] " + event.topic + ": " + redact_text(event.message));
   }
   if (lines.empty()) {
      lines.push_back("no events");
   }
   return lines;
}

std::vector<std::string> render_shell(const shell_model& model) {
   auto lines = std::vector<std::string>{};
   lines.push_back(model.title);
   lines.push_back("profile: " + model.profile + "  endpoint: " + redact_endpoint(model.endpoint) +
                   "  actor: " + redact_text(model.actor));
   lines.push_back("");
   lines.push_back("menu:");
   for (auto index = std::size_t{0}; index < model.navigation.items.size(); ++index) {
      const auto& item = model.navigation.items[index];
      const auto marker = index == model.navigation.selected ? "> " : "  ";
      lines.push_back(marker + item.label + " [" + to_string(item.state) + "]");
   }
   lines.push_back("");
   lines.push_back("content:");
   if (model.content_lines.empty()) {
      lines.push_back("not implemented in v1");
   } else {
      lines.insert(lines.end(), model.content_lines.begin(), model.content_lines.end());
   }
   lines.push_back("");
   lines.push_back("diagnostics: " + model.lifecycle_state);
   if (!model.last_error.empty()) {
      lines.push_back("last error: " + redact_text(model.last_error));
   }
   lines.push_back("");
   lines.push_back("events:");
   auto events = render_event_log(model.events);
   lines.insert(lines.end(), events.begin(), events.end());
   lines.push_back("");
   lines.push_back("q quit  enter select  esc back");
   return lines;
}

validation_result validate_form(const form_model& model) {
   auto result = validation_result{};
   for (const auto& field : model.fields) {
      if (field.required && field.value.empty()) {
         result.ok = false;
         result.errors.push_back(field.name.empty() ? field.label : field.name);
      }
      if (!field.error.empty()) {
         result.ok = false;
         result.errors.push_back(field.error);
      }
   }
   return result;
}

} // namespace forge::tui
