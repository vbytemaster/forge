module;

#include <string>
#include <string_view>
#include <vector>

export module forge.tui.render;

import forge.tui.types;

export namespace forge::tui {

[[nodiscard]] std::string to_string(severity value);
[[nodiscard]] std::string to_string(status value);
[[nodiscard]] std::string to_string(action_state value);
[[nodiscard]] std::string to_string(color_mode value);

[[nodiscard]] std::string redact_text(std::string_view value);
[[nodiscard]] std::vector<std::string> render_status_badge(const status_badge_model& model);
[[nodiscard]] std::vector<std::string> render_key_value_panel(const std::vector<key_value_item>& items);
[[nodiscard]] std::vector<std::string> render_table(const table_model& model);
[[nodiscard]] std::vector<std::string> render_form(const form_model& model);
[[nodiscard]] std::vector<std::string> render_modal(std::string_view title, const std::vector<std::string>& lines);
[[nodiscard]] std::vector<std::string> render_action_bar(const std::vector<navigation_item>& actions);
[[nodiscard]] std::vector<std::string> render_event_log(const event_log_model& model);
[[nodiscard]] std::vector<std::string> render_shell(const shell_model& model);
[[nodiscard]] validation_result validate_form(const form_model& model);

} // namespace forge::tui
