#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace forge::net::p2p::detail {

class connection_singleflight_registry {
 private:
   struct entry;

 public:
   struct outcome {
      bool succeeded = false;
      std::optional<exceptions::code> error;
      std::string message;
   };

   class lease {
    public:
      lease() = default;
      lease(const lease&) = delete;
      lease& operator=(const lease&) = delete;
      lease(lease&&) noexcept = default;
      lease& operator=(lease&&) noexcept = default;

      boost::asio::awaitable<outcome> wait();

    private:
      using completion_channel =
          boost::asio::experimental::concurrent_channel<void(boost::system::error_code, outcome)>;

      lease(peer_id peer, std::shared_ptr<entry> owner, std::shared_ptr<completion_channel> completion);

      peer_id peer_;
      std::shared_ptr<entry> owner_;
      std::shared_ptr<completion_channel> completion_;
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
      lease participant;
      std::optional<operation> start;
   };

   [[nodiscard]] joined join(const peer_id& peer, boost::asio::any_io_executor executor);
   void succeed(operation& active) noexcept;
   void fail(operation& active, exceptions::code error, std::string message) noexcept;
   void leave(lease& participant) noexcept;
   void close() noexcept;
   [[nodiscard]] std::size_t size() const noexcept;

 private:
   struct entry {
      outcome result;
      bool completed = false;
      bool operation_active = true;
      std::size_t participants = 0;
      std::vector<std::weak_ptr<lease::completion_channel>> completions;
   };

   void complete(entry& owner, outcome result) noexcept;
   void finish(operation& active, outcome result) noexcept;
   void erase_if_unused(const peer_id& peer, const std::shared_ptr<entry>& owner) noexcept;

   std::map<peer_id, std::shared_ptr<entry>> entries_;
};

} // namespace forge::net::p2p::detail
