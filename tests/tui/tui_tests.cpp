#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

import forge.app.application_shell;
import forge.app.events;
import forge.app.tui.dashboard;
import forge.tui.navigation;
import forge.tui.render;
import forge.tui.runner;
import forge.tui.types;
import forge.asio.blocking;

BOOST_AUTO_TEST_CASE(status_badge_renders_stable_text) {
   const auto lines = forge::tui::render_status_badge(forge::tui::status_badge_model{
       .value = forge::tui::status::degraded,
       .label = "service degraded",
   });
   BOOST_REQUIRE_EQUAL(lines.size(), 1);
   BOOST_TEST(lines[0] == "[degraded] service degraded");
}

BOOST_AUTO_TEST_CASE(table_states_render_without_terminal_backend) {
   auto loading = forge::tui::table_model{.loading = true};
   BOOST_TEST(forge::tui::render_table(loading)[0] == "loading...");

   auto empty = forge::tui::table_model{.empty_text = "no rows"};
   BOOST_TEST(forge::tui::render_table(empty)[0] == "no rows");

   auto error = forge::tui::table_model{.error = "failed"};
   BOOST_TEST(forge::tui::render_table(error)[0] == "error: failed");
}

BOOST_AUTO_TEST_CASE(key_value_panel_redacts_sensitive_values) {
   const auto lines = forge::tui::render_key_value_panel({
       {.key = "actor", .value = "operator"},
       {.key = "token", .value = "secret-token-value"},
       {.key = "workspace_secret", .value = "abc"},
       {.key = "path", .value = "/tmp/node"},
   });

   BOOST_REQUIRE_EQUAL(lines.size(), 4);
   BOOST_TEST(lines[0] == "actor: operator");
   BOOST_TEST(lines[1] == "token: <redacted>");
   BOOST_TEST(lines[2] == "workspace_secret: <redacted>");
   BOOST_TEST(lines[3] == "path: /tmp/node");
}

BOOST_AUTO_TEST_CASE(form_validation_reports_required_and_field_errors) {
   const auto result = forge::tui::validate_form(forge::tui::form_model{
       .fields =
           {
               {.name = "workspace", .label = "Workspace", .required = true},
               {.name = "endpoint", .label = "Endpoint", .value = "http://127.0.0.1:8888"},
               {.name = "grant", .label = "Grant", .value = "1", .error = "wrong scope"},
           },
   });

   BOOST_TEST(!result.ok);
   BOOST_REQUIRE_EQUAL(result.errors.size(), 2);
   BOOST_TEST(result.errors[0] == "workspace");
   BOOST_TEST(result.errors[1] == "wrong scope");
}

BOOST_AUTO_TEST_CASE(form_render_redacts_sensitive_field_identity) {
   const auto lines = forge::tui::render_form(forge::tui::form_model{
       .fields =
           {
               {.name = "token", .label = "Token", .value = "random-looking-value"},
               {.name = "endpoint", .label = "Private Key", .value = "random-looking-key"},
               {.name = "workspace", .label = "Workspace", .value = "public"},
           },
   });

   BOOST_REQUIRE_EQUAL(lines.size(), 3);
   BOOST_TEST(lines[0] == "Token: <redacted>");
   BOOST_TEST(lines[1] == "Private Key: <redacted>");
   BOOST_TEST(lines[2] == "Workspace: public");
}

BOOST_AUTO_TEST_CASE(shell_render_redacts_endpoint_credentials_and_queries) {
   const auto lines = forge::tui::render_shell(forge::tui::shell_model{
       .profile = "production",
       .endpoint = "https://user:pass@example.test:443/api?signature=abcdef&expires=1#frag",
       .actor = "operator",
   });

   BOOST_REQUIRE_GE(lines.size(), 2);
   BOOST_TEST(lines[1].find("user:pass") == std::string::npos);
   BOOST_TEST(lines[1].find("abcdef") == std::string::npos);
   BOOST_TEST(lines[1].find("https://<redacted>@example.test:443/api?<redacted>#frag") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(event_log_redacts_sensitive_messages) {
   const auto lines = forge::tui::render_event_log(forge::tui::event_log_model{
       .events =
           {
               {.level = forge::tui::severity::info, .topic = "runtime", .message = "started"},
               {.level = forge::tui::severity::critical, .topic = "auth", .message = "token abc"},
           },
       .max_items = 4,
   });

   BOOST_REQUIRE_EQUAL(lines.size(), 2);
   BOOST_TEST(lines[0].find("started") != std::string::npos);
   BOOST_TEST(lines[1].find("<redacted>") != std::string::npos);
   BOOST_TEST(lines[1].find("abc") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(app_tui_dashboard_model_uses_neutral_app_views) {
   auto shell = forge::app::application_shell{};
   shell.events().publish(forge::app::event_severity::info, "dashboard.test", "render me");

   auto model = forge::asio::blocking::run(
      shell.runtime(),
      forge::app::tui::build_dashboard_model(shell, {.query = {.limit = 10}, .max_events = 10}));

   BOOST_TEST(model.title == "FORGE Dashboard");
   BOOST_REQUIRE_GE(model.navigation.items.size(), 2);
   const auto has_item = [&](std::string_view id) {
      return std::ranges::any_of(model.navigation.items, [&](const auto& item) { return item.id == id; });
   };
   BOOST_TEST(has_item("forge.app.status"));
   BOOST_TEST(has_item("forge.app.events"));
   BOOST_TEST(std::ranges::any_of(model.content_lines, [](const auto& line) {
      return line.find("Application Status") != std::string::npos;
   }));
   BOOST_REQUIRE(!model.events.events.empty());
   BOOST_TEST(model.events.events.back().topic == "dashboard.test");
}

BOOST_AUTO_TEST_CASE(navigation_stack_push_pop_and_selection) {
   auto stack = forge::tui::navigation_stack{};
   stack.push(forge::tui::navigation_model{
       .items =
           {
               {.id = "apps", .label = "Applications"},
               {.id = "health", .label = "Health"},
           },
       .selected = 0,
   });

   BOOST_REQUIRE(stack.current_item().has_value());
   BOOST_TEST(stack.current_item()->id == "apps");
   stack.select_next();
   BOOST_REQUIRE(stack.current_item().has_value());
   BOOST_TEST(stack.current_item()->id == "health");
   stack.select_previous();
   BOOST_REQUIRE(stack.current_item().has_value());
   BOOST_TEST(stack.current_item()->id == "apps");
   BOOST_TEST(stack.pop());
   BOOST_TEST(stack.empty());
}

BOOST_AUTO_TEST_CASE(disabled_and_dangerous_action_states_are_visible) {
   const auto lines = forge::tui::render_action_bar({
       {.id = "approve", .label = "Approve", .state = forge::tui::action_state::disabled},
       {.id = "revoke", .label = "Revoke", .state = forge::tui::action_state::dangerous},
   });

   BOOST_REQUIRE_EQUAL(lines.size(), 1);
   BOOST_TEST(lines[0].find("disabled") != std::string::npos);
   BOOST_TEST(lines[0].find("dangerous") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(headless_runner_exits_through_injected_quit_event) {
   auto calls = 0;
   auto runner = forge::tui::screen_runner{};
   const auto rc = runner.run(forge::tui::screen_runner_options{
       .headless = true,
       .input = [&]() -> std::optional<forge::tui::input_event> {
          ++calls;
          return forge::tui::input_event{.value = forge::tui::input_event::kind::quit};
       },
       .model = [] { return forge::tui::shell_model{.title = "test"}; },
   });

   BOOST_TEST(rc == 0);
   BOOST_TEST(runner.stop_requested());
   BOOST_TEST(calls == 1);
}

BOOST_AUTO_TEST_CASE(terminal_capability_detection_does_not_throw) {
   const auto capabilities = forge::tui::detect_terminal_capabilities();
   if (!capabilities.available) {
      BOOST_TEST(!capabilities.degraded_reason.empty());
   }
}
