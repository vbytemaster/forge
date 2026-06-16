module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <utility>
#include <vector>

module fcl.plugins.p2p_pubsub.plugin;

import fcl.p2p.pubsub;
import fcl.plugins.p2p_pubsub.api;
import fcl.plugins.p2p_pubsub.types;

#include "details/join_flow.hxx"

namespace fcl::plugins::p2p_pubsub {

join_waiter::join_waiter(boost::asio::any_io_executor executor) : timer(std::move(executor)) {
   timer.expires_at(boost::asio::steady_timer::time_point::max());
}

void complete_join_waiter(std::shared_ptr<join_waiter> pending, std::exception_ptr error) {
   boost::asio::post(pending->timer.get_executor(), [pending = std::move(pending), error = std::move(error)]() mutable {
      pending->error = std::move(error);
      pending->ready = true;
      pending->timer.cancel(); // waiter_executor
   });
}

} // namespace fcl::plugins::p2p_pubsub
