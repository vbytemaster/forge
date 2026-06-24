module;

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module forge.app.views;

import forge.app.exceptions;
import forge.exceptions;

namespace forge::app {
namespace {

std::string current_error_message() {
   try {
      throw;
   } catch (const forge::exceptions::base& error) {
      return error.message();
   } catch (const std::exception& error) {
      return error.what();
   } catch (...) {
      return "unknown error";
   }
}

void normalize_query(view_query& query, std::uint64_t default_limit) {
   if (query.limit == 0) {
      query.limit = default_limit;
   }
}

} // namespace

struct view_registration::state {
   std::function<void(const std::string&)> unregister;
   std::string id;
   bool active = true;
};

struct view_registry::impl {
   struct source_entry {
      view_descriptor descriptor;
      std::shared_ptr<view_source> source;
   };

   explicit impl(std::uint64_t input_default_limit) : default_limit{input_default_limit == 0 ? 100 : input_default_limit} {}

   mutable std::mutex mutex;
   std::uint64_t default_limit = 100;
   std::vector<source_entry> sources;
};

view_registration::view_registration() = default;

view_registration::view_registration(std::shared_ptr<state> state) : state_{std::move(state)} {}

view_registration::~view_registration() {
   unregister();
}

view_registration::view_registration(view_registration&& other) noexcept = default;

view_registration& view_registration::operator=(view_registration&& other) noexcept {
   if (this != &other) {
      unregister();
      state_ = std::move(other.state_);
   }
   return *this;
}

void view_registration::unregister() noexcept {
   if (!state_) {
      return;
   }
   auto state = std::move(state_);
   if (!state->active) {
      return;
   }
   state->active = false;
   if (state->unregister) {
      state->unregister(state->id);
   }
}

bool view_registration::active() const noexcept {
   return state_ && state_->active;
}

view_registry::view_registry(std::uint64_t default_limit) : impl_{std::make_shared<impl>(default_limit)} {}

view_registry::~view_registry() = default;

view_registration view_registry::register_source(view_descriptor descriptor, std::shared_ptr<view_source> source) {
   if (descriptor.id.empty()) {
      throw exceptions::invalid_state{"view id must not be empty"};
   }
   if (descriptor.title.empty()) {
      descriptor.title = descriptor.id;
   }
   if (!source) {
      throw exceptions::invalid_state{"view source must not be null"};
   }

   auto lock = std::scoped_lock{impl_->mutex};
   if (std::ranges::any_of(impl_->sources, [&](const auto& entry) { return entry.descriptor.id == descriptor.id; })) {
      throw exceptions::invalid_state{"duplicate view source: " + descriptor.id};
   }

   const auto id = descriptor.id;
   impl_->sources.push_back(impl::source_entry{
      .descriptor = std::move(descriptor),
      .source = std::move(source),
   });

   auto state = std::make_shared<view_registration::state>();
   state->id = id;
   state->unregister = [owner = std::weak_ptr<impl>{impl_}](const std::string& source_id) {
      if (auto locked = owner.lock()) {
         auto owner_lock = std::scoped_lock{locked->mutex};
         std::erase_if(locked->sources, [&](const auto& entry) {
            return entry.descriptor.id == source_id;
         });
      }
   };
   return view_registration{std::move(state)};
}

void view_registry::unregister_source(const std::string& id) noexcept {
   auto lock = std::scoped_lock{impl_->mutex};
   std::erase_if(impl_->sources, [&](const auto& entry) {
      return entry.descriptor.id == id;
   });
}

std::vector<view_descriptor> view_registry::descriptors() const {
   auto out = std::vector<view_descriptor>{};
   auto lock = std::scoped_lock{impl_->mutex};
   out.reserve(impl_->sources.size());
   for (const auto& entry : impl_->sources) {
      out.push_back(entry.descriptor);
   }
   return out;
}

boost::asio::awaitable<view_snapshot> view_registry::snapshot(std::string id, view_query query) {
   normalize_query(query, impl_->default_limit);

   auto descriptor = view_descriptor{};
   auto source = std::shared_ptr<view_source>{};
   {
      auto lock = std::scoped_lock{impl_->mutex};
      const auto iterator = std::ranges::find_if(impl_->sources, [&](const auto& entry) {
         return entry.descriptor.id == id;
      });
      if (iterator == impl_->sources.end()) {
         throw exceptions::invalid_state{"view source not found: " + id};
      }
      descriptor = iterator->descriptor;
      source = iterator->source;
   }

   try {
      auto out = co_await source->snapshot(std::move(query));
      if (out.descriptor.id.empty()) {
         out.descriptor = std::move(descriptor);
      }
      co_return out;
   } catch (...) {
      co_return view_snapshot{
         .descriptor = std::move(descriptor),
         .error = current_error_message(),
      };
   }
}

boost::asio::awaitable<std::vector<view_snapshot>> view_registry::snapshots(view_query query) {
   normalize_query(query, impl_->default_limit);

   auto ids = std::vector<std::string>{};
   {
      auto lock = std::scoped_lock{impl_->mutex};
      ids.reserve(impl_->sources.size());
      for (const auto& entry : impl_->sources) {
         ids.push_back(entry.descriptor.id);
      }
   }

   auto out = std::vector<view_snapshot>{};
   out.reserve(ids.size());
   for (auto& id : ids) {
      out.push_back(co_await snapshot(std::move(id), query));
   }
   co_return out;
}

std::uint64_t view_registry::default_limit() const noexcept {
   return impl_->default_limit;
}

} // namespace forge::app
