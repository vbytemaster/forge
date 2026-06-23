module;

#include <functional>
#include <optional>

export module forge.tui.runner;

import forge.tui.types;

export namespace forge::tui {

struct screen_runner_options {
   bool headless = false;
   input_source input;
   std::function<shell_model()> model;
};

class screen_runner {
 public:
   screen_runner();
   ~screen_runner();

   screen_runner(const screen_runner&) = delete;
   screen_runner& operator=(const screen_runner&) = delete;

   [[nodiscard]] int run(screen_runner_options options);
   void request_stop() noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;
   [[nodiscard]] terminal_capabilities capabilities() const;

 private:
   bool stop_requested_ = false;
   terminal_capabilities capabilities_;
};

[[nodiscard]] terminal_capabilities detect_terminal_capabilities();

} // namespace forge::tui
