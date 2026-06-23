module;

#include <optional>
#include <vector>

export module forge.tui.navigation;

import forge.tui.types;

export namespace forge::tui {

class navigation_stack {
 public:
   navigation_stack();
   ~navigation_stack();

   navigation_stack(const navigation_stack&) = default;
   navigation_stack& operator=(const navigation_stack&) = default;
   navigation_stack(navigation_stack&&) noexcept = default;
   navigation_stack& operator=(navigation_stack&&) noexcept = default;

   void push(navigation_model model);
   bool pop();
   [[nodiscard]] bool empty() const noexcept;
   [[nodiscard]] const navigation_model* current() const noexcept;
   [[nodiscard]] navigation_model* current() noexcept;
   [[nodiscard]] std::optional<navigation_item> current_item() const;
   void select_next();
   void select_previous();

 private:
   std::vector<navigation_model> stack_;
};

} // namespace forge::tui
