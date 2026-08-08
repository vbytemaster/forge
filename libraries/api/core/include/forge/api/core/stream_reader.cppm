module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

export module forge.api.core.stream_reader;

export import forge.api.core.exceptions;
export import forge.api.core.types;
export import forge.raw.raw;

export namespace forge::api::core {

template <typename T>
class stream_reader;

namespace detail {

enum class stream_event {
   consumed,
   dropped,
};

class stream_endpoint {
 public:
   virtual ~stream_endpoint() = default;

   virtual boost::asio::awaitable<std::optional<bytes>> async_read() = 0;
   virtual boost::asio::awaitable<void> async_write(bytes value) = 0;
   virtual void close() noexcept = 0;
   virtual void fail(std::exception_ptr error) noexcept = 0;
   virtual void set_observer(
      std::function<void(stream_event, std::size_t)> observer) {
      static_cast<void>(observer);
   }
   virtual void set_failure_observer(std::function<void()> observer) {
      static_cast<void>(observer);
   }
};

struct local_stream_pair {
   std::shared_ptr<stream_endpoint> reader;
   std::shared_ptr<stream_endpoint> writer;
};

[[nodiscard]] local_stream_pair
make_local_stream_pair(boost::asio::any_io_executor executor,
                       std::size_t max_item_bytes,
                       std::size_t max_buffered_items,
                       std::size_t max_buffered_bytes);

class reader_access {
 public:
   template <typename T>
   [[nodiscard]] static stream_reader<T>
   make(std::shared_ptr<stream_endpoint> endpoint) {
      return stream_reader<T>{std::move(endpoint)};
   }

   template <typename T>
   [[nodiscard]] static const std::shared_ptr<stream_endpoint>&
   endpoint(const stream_reader<T>& value) {
      return value.endpoint_;
   }
};

} // namespace detail

template <typename T>
class stream_reader {
 public:
   stream_reader() = default;
   ~stream_reader() = default;

   stream_reader(stream_reader&&) noexcept = default;
   stream_reader& operator=(stream_reader&&) noexcept = default;

   stream_reader(const stream_reader&) = delete;
   stream_reader& operator=(const stream_reader&) = delete;

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(endpoint_);
   }

   boost::asio::awaitable<std::optional<T>> async_read() {
      return async_read_impl(endpoint_);
   }

 private:
   static boost::asio::awaitable<std::optional<T>>
   async_read_impl(std::shared_ptr<detail::stream_endpoint> endpoint) {
      if (!endpoint) {
         throw exceptions::protocol_error{"invalid API stream reader"};
      }
      auto payload = co_await endpoint->async_read();
      if (!payload) {
         co_return std::nullopt;
      }

      auto value = T{};
      forge::datastream<const std::uint8_t*> stream{
         payload->data(), payload->size()};
      forge::raw::unpack(stream, value);
      if (stream.remaining() != 0) {
         throw exceptions::protocol_error{
            "API stream item has trailing bytes"};
      }
      co_return value;
   }
   explicit stream_reader(std::shared_ptr<detail::stream_endpoint> endpoint)
       : endpoint_{std::move(endpoint)} {}

   std::shared_ptr<detail::stream_endpoint> endpoint_;

   friend class detail::reader_access;
};

template <typename T>
struct stream_reader_traits {
   static constexpr bool value = false;
};

template <typename T>
struct stream_reader_traits<stream_reader<T>> {
   static constexpr bool value = true;
   using item_type = T;
};

} // namespace forge::api::core
