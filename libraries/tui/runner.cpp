module;

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <notcurses/notcurses.h>

module forge.tui.runner;

import forge.tui.exceptions;
import forge.tui.render;
import forge.tui.types;

namespace forge::tui {
namespace {

class notcurses_session {
 public:
   notcurses_session() {
      auto options = notcurses_options{};
      options.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_DRAIN_INPUT;
      nc_ = notcurses_core_init(&options, nullptr);
      if (nc_ == nullptr) {
         throw exceptions::initialization_failed{"notcurses core initialization failed"};
      }
   }

   ~notcurses_session() {
      if (nc_ != nullptr) {
         (void)notcurses_stop(nc_);
      }
   }

   notcurses_session(const notcurses_session&) = delete;
   notcurses_session& operator=(const notcurses_session&) = delete;

   [[nodiscard]] notcurses* get() const noexcept {
      return nc_;
   }

 private:
   notcurses* nc_ = nullptr;
};

input_event map_input(std::uint32_t id) {
   if (id == 'q' || id == 'Q') {
      return {.value = input_event::kind::quit};
   }
   if (id == NCKEY_ESC) {
      return {.value = input_event::kind::back};
   }
   if (id == NCKEY_ENTER || id == NCKEY_RETURN || id == '\n' || id == '\r') {
      return {.value = input_event::kind::select};
   }
   if (id == NCKEY_UP) {
      return {.value = input_event::kind::up};
   }
   if (id == NCKEY_DOWN) {
      return {.value = input_event::kind::down};
   }
   return {};
}

void draw_lines(notcurses* nc, const std::vector<std::string>& lines) {
   auto* plane = notcurses_stdplane(nc);
   ncplane_erase(plane);
   for (auto row = std::size_t{0}; row < lines.size(); ++row) {
      (void)ncplane_putstr_yx(plane, static_cast<int>(row), 0, lines[row].c_str());
   }
   (void)notcurses_render(nc);
}

terminal_capabilities capabilities_from(notcurses* nc) {
   auto rows = unsigned{0};
   auto cols = unsigned{0};
   notcurses_stddim_yx(nc, &rows, &cols);
   return terminal_capabilities{
       .available = true,
       .colors = notcurses_cantruecolor(nc) ? color_mode::truecolor : color_mode::ansi_256,
       .unicode = notcurses_canutf8(nc),
       .width = cols,
       .height = rows,
   };
}

} // namespace

screen_runner::screen_runner() = default;
screen_runner::~screen_runner() = default;

int screen_runner::run(screen_runner_options options) {
   stop_requested_ = false;
   if (!options.model) {
      return 2;
   }

   if (options.headless) {
      (void)render_shell(options.model());
      if (options.input) {
         while (const auto event = options.input()) {
            if (event->value == input_event::kind::quit) {
               request_stop();
               break;
            }
         }
      }
      return 0;
   }

   try {
      auto session = notcurses_session{};
      capabilities_ = capabilities_from(session.get());
      while (!stop_requested_) {
         draw_lines(session.get(), render_shell(options.model()));
         if (options.input) {
            if (const auto event = options.input()) {
               if (event->value == input_event::kind::quit) {
                  request_stop();
               }
               continue;
            }
         }

         auto input = ncinput{};
         auto timeout = timespec{.tv_sec = 0, .tv_nsec = 100'000'000};
         const auto id = notcurses_get(session.get(), &timeout, &input);
         const auto event = map_input(id);
         if (event.value == input_event::kind::quit) {
            request_stop();
         }
      }
   } catch (const std::exception& error) {
      capabilities_ = terminal_capabilities{
          .available = false,
          .colors = color_mode::unknown,
          .unicode = false,
          .degraded_reason = error.what(),
      };
      return 5;
   }
   return 0;
}

void screen_runner::request_stop() noexcept {
   stop_requested_ = true;
}

bool screen_runner::stop_requested() const noexcept {
   return stop_requested_;
}

terminal_capabilities screen_runner::capabilities() const {
   return capabilities_;
}

terminal_capabilities detect_terminal_capabilities() {
   try {
      auto session = notcurses_session{};
      return capabilities_from(session.get());
   } catch (const std::exception& error) {
      return terminal_capabilities{
          .available = false,
          .colors = color_mode::unknown,
          .unicode = false,
          .degraded_reason = error.what(),
      };
   }
}

} // namespace forge::tui
