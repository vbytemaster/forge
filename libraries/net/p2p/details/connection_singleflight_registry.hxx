#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace forge::net::p2p::detail {

class lifecycle_wakeup;

class connection_singleflight_registry {
 private:
   struct entry;

 public:
   enum class test_stage : std::uint8_t {
      before_new_entry_publish,
      before_existing_entry_commit,
      before_completion_delivery,
   };

   struct test_hooks {
      void* context = nullptr;
      void (*reach)(void*, test_stage) = nullptr;
   };

   struct outcome {
      bool succeeded = false;
      std::optional<exceptions::code> error;
      std::string message;
   };

   enum class join_status : std::uint8_t {
      accepted,
      closed,
      backpressure,
   };

   class lease {
    public:
      lease() = default;
      lease(const lease&) = delete;
      lease& operator=(const lease&) = delete;
      lease(lease&&) noexcept = default;
      lease& operator=(lease&&) noexcept = default;

      boost::asio::awaitable<outcome> wait();
      void request_stop() noexcept;

    private:
      lease(peer_id peer, std::shared_ptr<entry> owner, bool queued);

      peer_id peer_;
      std::shared_ptr<entry> owner_;
      std::shared_ptr<std::atomic_bool> stop_requested_;
      bool queued_ = false;
      friend class connection_singleflight_registry;
   };

   class operation {
    public:
      operation() = default;
      operation(const operation&) = delete;
      operation& operator=(const operation&) = delete;
      operation(operation&&) noexcept = default;
      operation& operator=(operation&&) noexcept = default;

    private:
      operation(peer_id peer, std::shared_ptr<entry> owner);

      peer_id peer_;
      std::shared_ptr<entry> owner_;

      friend class connection_singleflight_registry;
   };

   struct joined {
      join_status status = join_status::accepted;
      lease participant;
      std::optional<operation> start;
   };

   connection_singleflight_registry() noexcept = default;
   explicit connection_singleflight_registry(test_hooks test_hooks) noexcept;

   [[nodiscard]] joined join(const peer_id& peer, boost::asio::any_io_executor executor,
                             std::size_t maximum_waiters = (std::numeric_limits<std::size_t>::max)());
   void succeed(operation& active) noexcept;
   void fail(operation& active, exceptions::code error, std::string message) noexcept;
   void rollback_unpublished(operation& active, lease& participant) noexcept;
   void leave(lease& participant) noexcept;
   void close() noexcept;
   [[nodiscard]] std::size_t size() const noexcept;

 private:
   struct entry {
      explicit entry(std::shared_ptr<lifecycle_wakeup> completion) noexcept;

      // completion_mutex publishes result exactly once before completion wakes
      // waiters; leases retain entry lifetime while reading the immutable value.
      mutable std::mutex completion_mutex;
      std::shared_ptr<lifecycle_wakeup> completion;
      outcome result;
      bool completed = false;
      bool operation_active = true;
      std::size_t participants = 0;
   };

   void complete(const std::shared_ptr<entry>& owner, outcome result) noexcept;
   void finish(operation& active, outcome result) noexcept;
   void erase_if_unused(const peer_id& peer, const std::shared_ptr<entry>& owner) noexcept;
   void reach_test_failpoint(test_stage stage) const;

   std::map<peer_id, std::shared_ptr<entry>> entries_;
   std::size_t queued_participants_ = 0;
   bool closed_ = false;
   test_hooks test_hooks_;
};

} // namespace forge::net::p2p::detail
