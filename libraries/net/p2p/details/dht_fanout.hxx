#pragma once

namespace forge::net::p2p {

class cancellation_latch;

namespace detail::dht_fanout {

enum class test_stage {
   before_coordinator_spawn,
   after_stop_request,
};

struct test_hooks {
   void* context = nullptr;
   void (*reach)(void*, test_stage) = nullptr;
};

struct request {
   std::vector<peer_id> peers;
   std::size_t concurrency = 0;
   std::size_t success_target = 0;
   std::chrono::milliseconds timeout{};
   std::string operation;
   test_hooks hooks{};
};

struct result {
   std::size_t succeeded = 0;
   std::size_t attempted = 0;
};

using operation = std::function<boost::asio::awaitable<bool>(
    const peer_id&, std::chrono::milliseconds, std::shared_ptr<cancellation_latch>)>;

boost::asio::awaitable<result> run(boost::asio::io_context& context, request value, operation invoke);

} // namespace detail::dht_fanout
} // namespace forge::net::p2p
