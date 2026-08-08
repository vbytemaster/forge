module;

#include <boost/asio/awaitable.hpp>

#include <optional>
#include <type_traits>
#include <utility>

export module forge.api.core.duplex_stream;

export import forge.api.core.stream_reader;
export import forge.api.core.stream_writer;

export namespace forge::api::core {

template <typename Incoming, typename Outgoing>
class duplex_stream {
 public:
   duplex_stream() = default;
   duplex_stream(stream_reader<Incoming> input,
                 stream_writer<Outgoing> output)
       : input_{std::move(input)}, output_{std::move(output)} {}

   duplex_stream(duplex_stream&&) noexcept = default;
   duplex_stream& operator=(duplex_stream&&) noexcept = default;

   duplex_stream(const duplex_stream&) = delete;
   duplex_stream& operator=(const duplex_stream&) = delete;

   [[nodiscard]] stream_reader<Incoming>& input() noexcept {
      return input_;
   }

   [[nodiscard]] stream_writer<Outgoing>& output() noexcept {
      return output_;
   }

   boost::asio::awaitable<std::optional<Incoming>> async_read() {
      co_return co_await input_.async_read();
   }

   boost::asio::awaitable<void> async_write(Outgoing value) {
      co_await output_.async_write(std::move(value));
   }

   boost::asio::awaitable<void> async_close() {
      co_await output_.async_close();
   }

 private:
   stream_reader<Incoming> input_;
   stream_writer<Outgoing> output_;
};

template <typename T>
struct duplex_stream_traits {
   static constexpr bool value = false;
};

template <typename Incoming, typename Outgoing>
struct duplex_stream_traits<duplex_stream<Incoming, Outgoing>> {
   static constexpr bool value = true;
   using input_type = Incoming;
   using output_type = Outgoing;
};

template <typename T>
inline constexpr bool stream_endpoint_v =
   stream_reader_traits<std::remove_cvref_t<T>>::value ||
   stream_writer_traits<std::remove_cvref_t<T>>::value ||
   duplex_stream_traits<std::remove_cvref_t<T>>::value;

} // namespace forge::api::core
