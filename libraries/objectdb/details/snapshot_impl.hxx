#pragma once

#include <memory>

namespace forge::objectdb {

struct snapshot::impl {
   impl(std::unique_ptr<session> active_value, snapshot::ensure_registered_fn ensure) noexcept;

   std::unique_ptr<session> active;
   snapshot::ensure_registered_fn ensure_registered;
   bool closed = false;
};

} // namespace forge::objectdb
