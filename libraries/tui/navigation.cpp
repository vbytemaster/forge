module;

#include <algorithm>
#include <optional>
#include <utility>

module forge.tui.navigation;

import forge.tui.types;

namespace forge::tui {

navigation_stack::navigation_stack() = default;
navigation_stack::~navigation_stack() = default;

void navigation_stack::push(navigation_model model) {
   if (!model.items.empty() && model.selected >= model.items.size()) {
      model.selected = model.items.size() - 1;
   }
   stack_.push_back(std::move(model));
}

bool navigation_stack::pop() {
   if (stack_.empty()) {
      return false;
   }
   stack_.pop_back();
   return true;
}

bool navigation_stack::empty() const noexcept {
   return stack_.empty();
}

const navigation_model* navigation_stack::current() const noexcept {
   if (stack_.empty()) {
      return nullptr;
   }
   return &stack_.back();
}

navigation_model* navigation_stack::current() noexcept {
   if (stack_.empty()) {
      return nullptr;
   }
   return &stack_.back();
}

std::optional<navigation_item> navigation_stack::current_item() const {
   const auto* model = current();
   if (model == nullptr || model->items.empty() || model->selected >= model->items.size()) {
      return std::nullopt;
   }
   return model->items[model->selected];
}

void navigation_stack::select_next() {
   auto* model = current();
   if (model == nullptr || model->items.empty()) {
      return;
   }
   model->selected = std::min(model->selected + 1, model->items.size() - 1);
}

void navigation_stack::select_previous() {
   auto* model = current();
   if (model == nullptr || model->items.empty() || model->selected == 0) {
      return;
   }
   --model->selected;
}

} // namespace forge::tui
