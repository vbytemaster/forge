module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

export module forge.objectdb.session;

import forge.objectdb.cursor;
import forge.objectdb.exceptions;
import forge.objectdb.record;

export namespace forge::objectdb {

struct capabilities {
   bool snapshot_reads = false;
   bool writes = true;
};

class session {
 public:
   virtual ~session() = default;

   [[nodiscard]] virtual forge::objectdb::capabilities capabilities() const noexcept = 0;
   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(record_key key) = 0;
   virtual boost::asio::awaitable<void> put(record_key key, std::vector<std::byte> value) = 0;
   virtual boost::asio::awaitable<void> erase(record_key key) = 0;
   virtual boost::asio::awaitable<record_page> scan_page(record_range range, page_request request) = 0;
   virtual boost::asio::awaitable<void> commit() = 0;
   virtual boost::asio::awaitable<void> rollback() = 0;
};

template <typename T>
concept session_model = std::derived_from<T, session>;

template <session_model Session>
class session_factory {
 public:
   using begin_fn = std::function<boost::asio::awaitable<std::unique_ptr<Session>>()>;

   explicit session_factory(begin_fn begin) : begin_{std::move(begin)} {}

   boost::asio::awaitable<std::unique_ptr<session>> begin() const {
      if (!begin_) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb session factory is empty");
      }
      auto native = co_await begin_();
      if (!native) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb session factory returned null session");
      }
      std::unique_ptr<session> erased = std::move(native);
      co_return erased;
   }

 private:
   begin_fn begin_;
};

} // namespace forge::objectdb
