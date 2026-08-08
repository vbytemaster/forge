module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

module forge.api.core.stream_reader;

#include "details/stream_state.hxx"

namespace forge::api::core::detail {

local_stream_pair
make_local_stream_pair(boost::asio::any_io_executor executor,
                       std::size_t max_item_bytes,
                       std::size_t max_buffered_items,
                       std::size_t max_buffered_bytes) {
   static_cast<void>(executor);
   auto state = std::make_shared<stream_state>(
      max_item_bytes, max_buffered_items, max_buffered_bytes);
   return local_stream_pair{.reader = state, .writer = std::move(state)};
}

} // namespace forge::api::core::detail
