#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/compat/move_only_function.hpp>
#include <atomic>
#include <functional>
#include <memory>

namespace forge::net::p2p::detail {

class worker_stop_bridge;
class worker_terminal_owner;
class lifecycle_stop_source;
struct worker_stop_bridge_state;
struct worker_stop_bridge_run_state;

struct worker_stop_bridge_options {
   // The bridge keeps its lifecycle listener registered until both stop and
   // work branches are joined.
   std::shared_ptr<lifecycle_stop_source> lifecycle_stop;
   std::function<void()> before_stop_wait;
   std::function<void()> before_work_spawn;
};

using worker_stop_work =
    std::function<boost::asio::awaitable<void>(std::shared_ptr<worker_terminal_owner>)>;

boost::asio::awaitable<void> async_run_with_stop_bridge(std::shared_ptr<worker_stop_bridge> stop,
                                                         worker_stop_work work,
                                                         worker_stop_bridge_options options = {});

// Executor-confined terminal ownership for one worker. Work publishes its
// concrete no-throw cancellation before its first blocking suspension. A stop
// requested before publication is sticky and invokes that callback once when
// it appears; natural completion seals it without cancellation.
class worker_terminal_owner final {
 public:
   using callback = boost::compat::move_only_function<void() noexcept>;

   explicit worker_terminal_owner(std::shared_ptr<worker_stop_bridge_state> stop_state);

   [[nodiscard]] bool publish(callback cancel) noexcept;
   void request_stop() noexcept;
   void seal() noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;

 private:
   enum class state { open, stop_requested, sealed };

   std::shared_ptr<worker_stop_bridge_state> stop_state_;
   callback cancel_;
   state state_ = state::open;
   bool published_ = false;
   bool invoked_ = false;
};

// A bridge is shared with threads that may stop a worker. They only publish a
// sticky notification; the worker executor owns the composed cancellation race.
class worker_stop_bridge final {
 public:
   worker_stop_bridge();

   void request_stop() noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;

 private:
   friend boost::asio::awaitable<void> async_run_with_stop_bridge(std::shared_ptr<worker_stop_bridge>,
                                                                    worker_stop_work,
                                                                    worker_stop_bridge_options);

   std::shared_ptr<worker_stop_bridge_state> state_;
};

} // namespace forge::net::p2p::detail
