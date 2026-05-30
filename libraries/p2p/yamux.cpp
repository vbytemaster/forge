module;

#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.node;

import fcl.transport.exceptions;
import fcl.transport.stream;

#include "yamux.hpp"

namespace fcl::p2p::yamux {
namespace {

enum class frame_type : std::uint8_t {
   data = 0,
   window_update = 1,
   ping = 2,
   go_away = 3,
};

inline constexpr std::uint16_t syn = 0x01;
inline constexpr std::uint16_t ack = 0x02;
inline constexpr std::uint16_t fin = 0x04;
inline constexpr std::uint16_t rst = 0x08;

} // namespace

struct session::frame {
   frame_type kind = frame_type::data;
   std::uint16_t flags = 0;
   std::uint32_t stream_id = 0;
   std::vector<std::uint8_t> payload;
   std::uint32_t length_value = 0;
};

class session::stream_concept final : public fcl::transport::detail::stream_concept {
 public:
   stream_concept(std::shared_ptr<session> owner, std::uint32_t stream_id)
       : owner_(std::move(owner)), stream_id_(stream_id) {}

   [[nodiscard]] bool valid() const noexcept override {
      return owner_ != nullptr;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return static_cast<std::int64_t>(stream_id_);
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      co_await owner_->write_data(stream_id_, bytes);
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      co_return co_await owner_->read_data(stream_id_);
   }

   boost::asio::awaitable<void> async_close() override {
      co_await owner_->close_stream(stream_id_);
   }

 private:
   std::shared_ptr<session> owner_;
   std::uint32_t stream_id_ = 0;
};

session::session(fcl::transport::stream stream, bool initiator, options options_value)
    : stream_(std::move(stream)), options_(options_value), next_stream_id_(initiator ? 1U : 2U) {}

boost::asio::awaitable<fcl::transport::stream> session::async_open_stream() {
   const auto id = next_stream_id_;
   next_stream_id_ += 2;
   co_await write_frame(frame{
       .kind = frame_type::window_update,
       .flags = syn,
       .stream_id = id,
       .length_value = options_.initial_window,
   });
   co_return fcl::transport::detail::stream_access::make(std::make_shared<stream_concept>(shared_from_this(), id));
}

boost::asio::awaitable<fcl::transport::stream> session::async_accept_stream() {
   if (!pending_streams_.empty()) {
      const auto id = pending_streams_.front();
      pending_streams_.erase(pending_streams_.begin());
      co_return fcl::transport::detail::stream_access::make(std::make_shared<stream_concept>(shared_from_this(), id));
   }
   while (true) {
      auto value = co_await read_frame();
      if (value.kind == frame_type::ping && (value.flags & ack) == 0) {
         co_await write_frame(frame{
             .kind = frame_type::ping,
             .flags = ack,
             .stream_id = 0,
             .payload = value.payload,
             .length_value = value.length_value,
         });
         continue;
      }
      if (value.kind == frame_type::go_away) {
         FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "Yamux session closed");
      }
      if (value.kind != frame_type::data && value.kind != frame_type::window_update) {
         continue;
      }
      if ((value.flags & syn) == 0) {
         if (!value.payload.empty()) {
            pending_payloads_[value.stream_id].push_back(std::move(value.payload));
         }
         continue;
      }
      co_await accept_remote_stream(value.stream_id);
      if (!value.payload.empty()) {
         pending_payloads_[value.stream_id].push_back(std::move(value.payload));
      }
      co_return fcl::transport::detail::stream_access::make(
          std::make_shared<stream_concept>(shared_from_this(), value.stream_id));
   }
}

bool session::has_pending_stream() const noexcept {
   return !pending_streams_.empty();
}

boost::asio::awaitable<void> session::async_close() {
   pending_payloads_.clear();
   co_await stream_.async_close();
}

boost::asio::awaitable<void> session::write_data(std::uint32_t stream_id, std::span<const std::uint8_t> bytes,
                                                 std::uint16_t flags) {
   co_await write_frame(frame{
       .kind = frame_type::data,
       .flags = flags,
       .stream_id = stream_id,
       .payload = std::vector<std::uint8_t>{bytes.begin(), bytes.end()},
   });
}

boost::asio::awaitable<std::vector<std::uint8_t>> session::read_data(std::uint32_t stream_id) {
   if (auto it = pending_payloads_.find(stream_id); it != pending_payloads_.end() && !it->second.empty()) {
      auto out = std::move(it->second.front());
      it->second.erase(it->second.begin());
      co_return out;
   }
   while (true) {
      auto value = co_await read_frame();
      if (value.kind == frame_type::ping && (value.flags & ack) == 0) {
         co_await write_frame(frame{
             .kind = frame_type::ping,
             .flags = ack,
             .stream_id = 0,
             .payload = value.payload,
             .length_value = value.length_value,
         });
         continue;
      }
      if (value.stream_id != stream_id) {
         if ((value.kind == frame_type::data || value.kind == frame_type::window_update) && (value.flags & syn) != 0) {
            co_await accept_remote_stream(value.stream_id);
            if (std::ranges::find(pending_streams_, value.stream_id) == pending_streams_.end()) {
               pending_streams_.push_back(value.stream_id);
            }
         }
         if (!value.payload.empty()) {
            pending_payloads_[value.stream_id].push_back(std::move(value.payload));
         }
         continue;
      }
      if ((value.flags & rst) != 0) {
         FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "Yamux stream was reset");
      }
      if (!value.payload.empty()) {
         co_return value.payload;
      }
      if ((value.flags & fin) != 0) {
         FCL_THROW_EXCEPTION(fcl::transport::exceptions::closed, "Yamux stream closed");
      }
   }
}

boost::asio::awaitable<void> session::close_stream(std::uint32_t stream_id) {
   co_await write_frame(frame{
       .kind = frame_type::data,
       .flags = fin,
       .stream_id = stream_id,
   });
}

boost::asio::awaitable<void> session::accept_remote_stream(std::uint32_t stream_id) {
   co_await write_frame(frame{
       .kind = frame_type::window_update,
       .flags = ack,
       .stream_id = stream_id,
       .length_value = options_.initial_window,
   });
}

boost::asio::awaitable<void> session::write_frame(const frame& value) {
   auto out = std::vector<std::uint8_t>{};
   const auto length = value.payload.empty() ? value.length_value : static_cast<std::uint32_t>(value.payload.size());
   out.reserve(12 + value.payload.size());
   out.push_back(0);
   out.push_back(static_cast<std::uint8_t>(value.kind));
   out.push_back(static_cast<std::uint8_t>((value.flags >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(value.flags & 0xffU));
   for (auto shift : {24, 16, 8, 0}) {
      out.push_back(static_cast<std::uint8_t>((value.stream_id >> shift) & 0xffU));
   }
   for (auto shift : {24, 16, 8, 0}) {
      out.push_back(static_cast<std::uint8_t>((length >> shift) & 0xffU));
   }
   out.insert(out.end(), value.payload.begin(), value.payload.end());
   co_await stream_.async_write(out);
}

boost::asio::awaitable<session::frame> session::read_frame() {
   while (buffer_.size() < 12) {
      auto chunk = co_await stream_.async_read();
      buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
   }
   if (buffer_[0] != 0) {
      FCL_THROW_EXCEPTION(fcl::transport::exceptions::protocol_error, "unsupported Yamux version");
   }
   auto value = frame{
       .kind = static_cast<frame_type>(buffer_[1]),
       .flags = static_cast<std::uint16_t>((buffer_[2] << 8U) | buffer_[3]),
       .stream_id = (static_cast<std::uint32_t>(buffer_[4]) << 24U) |
                    (static_cast<std::uint32_t>(buffer_[5]) << 16U) |
                    (static_cast<std::uint32_t>(buffer_[6]) << 8U) | buffer_[7],
       .length_value = (static_cast<std::uint32_t>(buffer_[8]) << 24U) |
                       (static_cast<std::uint32_t>(buffer_[9]) << 16U) |
                       (static_cast<std::uint32_t>(buffer_[10]) << 8U) | buffer_[11],
   };
   const auto payload_length = value.kind == frame_type::data ? value.length_value : 0U;
   while (buffer_.size() < 12ULL + payload_length) {
      auto chunk = co_await stream_.async_read();
      buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
   }
   if (payload_length > 0) {
      value.payload.assign(buffer_.begin() + 12,
                           buffer_.begin() + static_cast<std::ptrdiff_t>(12ULL + payload_length));
   }
   buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(12ULL + payload_length));
   co_return value;
}

} // namespace fcl::p2p::yamux
