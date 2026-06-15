module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.http.body;

export namespace fcl::http {

struct body_chunk {
   std::vector<std::byte> bytes;
};

struct stream_limits {
   std::uint64_t max_body_bytes = 16 * 1024 * 1024;
   std::uint64_t max_chunk_bytes = 64 * 1024;
   std::chrono::milliseconds read_timeout{30'000};
   std::chrono::milliseconds write_timeout{30'000};
};

class body_reader {
 public:
   class source {
    public:
      virtual ~source() = default;

      virtual boost::asio::awaitable<std::optional<body_chunk>> async_read() = 0;
      [[nodiscard]] virtual std::uint64_t bytes_read() const noexcept = 0;
   };

   body_reader() = default;
   explicit body_reader(std::shared_ptr<source> source_value) : source_(std::move(source_value)) {}

   [[nodiscard]] bool valid() const noexcept {
      return static_cast<bool>(source_);
   }

   boost::asio::awaitable<std::optional<body_chunk>> async_read() {
      if (!source_) {
         co_return std::nullopt;
      }
      co_return co_await source_->async_read();
   }

   boost::asio::awaitable<std::string> async_read_all() {
      auto output = std::string{};
      while (auto chunk = co_await async_read()) {
         output.append(reinterpret_cast<const char*>(chunk->bytes.data()), chunk->bytes.size());
      }
      co_return output;
   }

   [[nodiscard]] std::uint64_t bytes_read() const noexcept {
      return source_ ? source_->bytes_read() : 0;
   }

 private:
   std::shared_ptr<source> source_;
};

class body_writer {
 public:
   using sink = std::function<boost::asio::awaitable<void>(body_chunk)>;

   body_writer() = default;
   explicit body_writer(sink sink_value) : sink_(std::move(sink_value)) {}

   [[nodiscard]] bool valid() const noexcept {
      return static_cast<bool>(sink_);
   }

   boost::asio::awaitable<void> async_write(body_chunk chunk) {
      if (!sink_) {
         co_return;
      }
      co_await sink_(std::move(chunk));
   }

 private:
   sink sink_;
};

} // namespace fcl::http
