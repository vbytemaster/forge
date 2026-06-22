#pragma once

namespace fcl::plugins::p2p::pubsub {

struct handler_record {
   std::uint64_t id = 0;
   fcl::p2p::pubsub::topic subject;
   handler callback;
   std::chrono::milliseconds deadline{0};
};

struct join_waiter {
   explicit join_waiter(boost::asio::any_io_executor executor);

   boost::asio::steady_timer timer;
   std::exception_ptr error;
   bool ready = false;
};

struct topic_state {
   std::map<std::uint64_t, handler_record> handlers;
   std::vector<std::shared_ptr<join_waiter>> waiters;
   bool joining = false;
   bool joined = false;
};

void complete_join_waiter(std::shared_ptr<join_waiter> pending, std::exception_ptr error = {});

} // namespace fcl::plugins::p2p::pubsub
