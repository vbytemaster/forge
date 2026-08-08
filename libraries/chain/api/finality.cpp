module;

#include <forge/exceptions/macros.hpp>

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

module forge.chain.api.finality;

import forge.chain.api.exceptions;

namespace forge::chain::api {

std::optional<protocol::block_id> finality_verifier::preferred_trust_anchor() const {
   return std::nullopt;
}

namespace {

[[noreturn]] void throw_anchor_collision(const protocol::state_anchor& anchor) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                         "finality anchor conflicts with a cached anchor for the same block",
                         forge::exceptions::ctx("block", anchor.block.str()));
}

template <typename Function> void verify_delegate(Function&& function) {
   try {
      std::forward<Function>(function)();
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality, "finality verifier failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality, "finality verifier failed");
   }
}

} // namespace

struct cached_finality_verifier::impl {
   struct cache_entry {
      protocol::state_anchor anchor;
      std::list<std::string>::iterator position;
   };

   struct pending_verification {
      explicit pending_verification(protocol::state_anchor value) : anchor(std::move(value)) {}

      protocol::state_anchor anchor;
      bool complete = false;
      std::exception_ptr failure;
      std::condition_variable ready;
   };

   impl(std::shared_ptr<finality_verifier> verifier, std::size_t max_entries)
       : delegate(std::move(verifier)), capacity(max_entries) {}

   bool use_cached_locked(const protocol::state_anchor& anchor, const std::string& key) {
      const auto found = cache.find(key);
      if (found == cache.end()) {
         return false;
      }
      if (found->second.anchor != anchor) {
         throw_anchor_collision(anchor);
      }

      order.splice(order.begin(), order, found->second.position);
      return true;
   }

   void validate_pending_locked(const protocol::state_anchor& anchor, const std::string& key) const {
      const auto found = pending.find(key);
      if (found != pending.end() && found->second->anchor != anchor) {
         throw_anchor_collision(anchor);
      }
   }

   void store_locked(const protocol::state_anchor& anchor, const std::string& key) {
      if (use_cached_locked(anchor, key)) {
         return;
      }

      order.push_front(key);
      try {
         cache.emplace(key, cache_entry{anchor, order.begin()});
      } catch (...) {
         order.pop_front();
         throw;
      }

      while (cache.size() > capacity) {
         cache.erase(order.back());
         order.pop_back();
      }
   }

   std::shared_ptr<finality_verifier> delegate;
   std::size_t capacity;
   std::mutex mutex;
   std::list<std::string> order;
   std::unordered_map<std::string, cache_entry> cache;
   std::unordered_map<std::string, std::shared_ptr<pending_verification>> pending;
};

cached_finality_verifier::cached_finality_verifier(std::shared_ptr<finality_verifier> delegate, std::size_t capacity) {
   if (!delegate) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "finality verifier delegate is required");
   }
   if (capacity == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "finality cache capacity must be positive");
   }
   impl_ = std::make_unique<impl>(std::move(delegate), capacity);
}

cached_finality_verifier::~cached_finality_verifier() = default;

cached_finality_verifier::cached_finality_verifier(cached_finality_verifier&&) noexcept = default;

cached_finality_verifier& cached_finality_verifier::operator=(cached_finality_verifier&&) noexcept = default;

std::optional<protocol::block_id> cached_finality_verifier::preferred_trust_anchor() const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "cached finality verifier is not initialized");
   }
   try {
      return impl_->delegate->preferred_trust_anchor();
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::anchor_unavailable, "finality trust anchor provider failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::anchor_unavailable, "finality trust anchor provider failed");
   }
}

void cached_finality_verifier::verify(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "cached finality verifier is not initialized");
   }

   const auto key = anchor.block.str();
   std::shared_ptr<impl::pending_verification> pending;
   {
      auto lock = std::unique_lock{impl_->mutex};
      if (impl_->use_cached_locked(anchor, key)) {
         return;
      }

      const auto found = impl_->pending.find(key);
      if (found != impl_->pending.end()) {
         pending = found->second;
         if (pending->anchor != anchor) {
            throw_anchor_collision(anchor);
         }

         pending->ready.wait(lock, [&pending] { return pending->complete; });
         const auto failure = pending->failure;
         lock.unlock();
         if (failure) {
            std::rethrow_exception(failure);
         }
         return;
      }

      pending = std::make_shared<impl::pending_verification>(anchor);
      impl_->pending.emplace(key, pending);
   }

   try {
      verify_delegate([&] { impl_->delegate->verify(anchor, proof); });
      {
         const auto lock = std::lock_guard{impl_->mutex};
         impl_->store_locked(anchor, key);
         pending->complete = true;
         impl_->pending.erase(key);
      }
      pending->ready.notify_all();
   } catch (...) {
      const auto failure = std::current_exception();
      {
         const auto lock = std::lock_guard{impl_->mutex};
         pending->failure = failure;
         pending->complete = true;
         const auto found = impl_->pending.find(key);
         if (found != impl_->pending.end() && found->second == pending) {
            impl_->pending.erase(found);
         }
      }
      pending->ready.notify_all();
      std::rethrow_exception(failure);
   }
}

void cached_finality_verifier::verify_ancestry(const protocol::state_anchor& finalized,
                                               std::span<const protocol::state_anchor> intermediate,
                                               const protocol::proof_blob& proof) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "cached finality verifier is not initialized");
   }

   const auto key = finalized.block.str();
   {
      const auto lock = std::lock_guard{impl_->mutex};
      impl_->use_cached_locked(finalized, key);
      impl_->validate_pending_locked(finalized, key);
   }

   verify_delegate([&] { impl_->delegate->verify_ancestry(finalized, intermediate, proof); });

   {
      const auto lock = std::lock_guard{impl_->mutex};
      impl_->validate_pending_locked(finalized, key);
      impl_->store_locked(finalized, key);
   }
}

} // namespace forge::chain::api
