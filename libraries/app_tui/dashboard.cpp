module;

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

module forge.app.tui.dashboard;

import forge.app.application_shell;
import forge.app.diagnostics;
import forge.app.views;
import forge.tui.types;

namespace forge::app::tui {
namespace {

forge::tui::severity severity_from(view_severity value) {
   switch (value) {
   case view_severity::debug:
   case view_severity::info:
      return forge::tui::severity::info;
   case view_severity::warning:
      return forge::tui::severity::warning;
   case view_severity::error:
      return forge::tui::severity::error;
   case view_severity::critical:
      return forge::tui::severity::critical;
   }
   return forge::tui::severity::info;
}

std::string lifecycle_state_name(lifecycle_state state) {
   switch (state) {
   case lifecycle_state::created:
      return "created";
   case lifecycle_state::initializing:
      return "initializing";
   case lifecycle_state::initialized:
      return "initialized";
   case lifecycle_state::starting:
      return "starting";
   case lifecycle_state::started:
      return "started";
   case lifecycle_state::stopping:
      return "stopping";
   case lifecycle_state::stopped:
      return "stopped";
   case lifecycle_state::failed:
      return "failed";
   }
   return "unknown";
}

std::string join_cells(const std::vector<std::string>& cells) {
   auto out = std::ostringstream{};
   for (auto index = std::size_t{0}; index < cells.size(); ++index) {
      if (index != 0) {
         out << " | ";
      }
      out << cells[index];
   }
   return out.str();
}

void append_snapshot_lines(std::vector<std::string>& lines, const view_snapshot& snapshot) {
   lines.push_back("== " + snapshot.descriptor.title + " ==");
   if (!snapshot.error.empty()) {
      lines.push_back("error: " + snapshot.error);
      return;
   }
   for (const auto& counter : snapshot.counters) {
      lines.push_back(counter.name + ": " + counter.value);
   }
   if (!snapshot.columns.empty()) {
      auto headings = std::vector<std::string>{};
      headings.reserve(snapshot.columns.size());
      for (const auto& column : snapshot.columns) {
         headings.push_back(column.title);
      }
      lines.push_back(join_cells(headings));
   }
   for (const auto& row : snapshot.page.rows) {
      lines.push_back(join_cells(row.cells));
   }
   if (snapshot.page.next_cursor.has_value()) {
      lines.push_back("next: " + *snapshot.page.next_cursor);
   }
   if (snapshot.counters.empty() && snapshot.columns.empty() && snapshot.page.rows.empty() && snapshot.log.empty() &&
       snapshot.actions.empty()) {
      lines.push_back("empty");
   }
}

forge::tui::navigation_model navigation_from(const std::vector<view_snapshot>& snapshots) {
   auto model = forge::tui::navigation_model{};
   model.items.reserve(snapshots.size());
   for (const auto& snapshot : snapshots) {
      model.items.push_back(forge::tui::navigation_item{
         .id = snapshot.descriptor.id,
         .label = snapshot.descriptor.title,
      });
   }
   return model;
}

forge::tui::event_log_model event_log_from(const std::vector<view_snapshot>& snapshots, std::size_t max_events) {
   auto model = forge::tui::event_log_model{.max_items = max_events};
   for (const auto& snapshot : snapshots) {
      if (snapshot.descriptor.kind != view_kind::log) {
         continue;
      }
      for (const auto& item : snapshot.log) {
         model.events.push_back(forge::tui::event_item{
            .level = severity_from(item.severity),
            .topic = item.topic,
            .message = item.message,
         });
      }
   }
   return model;
}

} // namespace

boost::asio::awaitable<forge::tui::shell_model>
build_dashboard_model(forge::app::application_shell& shell, dashboard_options options) {
   auto query = std::move(options.query);
   if (query.limit == 0) {
      query.limit = shell.views().default_limit();
   }
   auto snapshots = co_await shell.views().snapshots(std::move(query));
   const auto diagnostics = shell.diagnostics().snapshot(shell.events());

   auto model = forge::tui::shell_model{
      .title = "FORGE Dashboard",
      .navigation = navigation_from(snapshots),
      .events = event_log_from(snapshots, options.max_events),
      .lifecycle_state = lifecycle_state_name(diagnostics.state),
      .last_error = diagnostics.last_error,
   };

   for (const auto& snapshot : snapshots) {
      append_snapshot_lines(model.content_lines, snapshot);
      model.content_lines.push_back("");
   }
   co_return model;
}

} // namespace forge::app::tui
