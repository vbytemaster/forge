module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <string>

export module forge.app.tui.dashboard;

import forge.app.application_shell;
import forge.app.views;
import forge.tui.types;

export namespace forge::app::tui {

struct dashboard_options {
   forge::app::view_query query{};
   std::size_t max_events = 10;
};

[[nodiscard]] boost::asio::awaitable<forge::tui::shell_model>
build_dashboard_model(forge::app::application_shell& shell, dashboard_options options = {});

} // namespace forge::app::tui
