module;

#include <fcl/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module fcl.transport.api.server;

import fcl.raw.raw;
import fcl.transport.exceptions;
import fcl.transport.frame;

namespace fcl::transport::api {
namespace {

constexpr auto compact_threshold = std::size_t{65'536};

void compact_buffer(std::vector<std::uint8_t>& buffer, std::size_t& consumed) {
   if (consumed == 0) {
      return;
   }
   if (consumed >= buffer.size()) {
      buffer.clear();
      consumed = 0;
      return;
   }
   auto compacted = std::vector<std::uint8_t>{};
   compacted.reserve(buffer.size() - consumed);
   compacted.insert(compacted.end(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed), buffer.end());
   buffer = std::move(compacted);
   consumed = 0;
}

[[nodiscard]] std::span<const std::uint8_t> available_bytes(const std::vector<std::uint8_t>& buffer,
                                                            std::size_t consumed) noexcept {
   if (consumed >= buffer.size()) {
      return {};
   }
   return {buffer.data() + consumed, buffer.size() - consumed};
}

boost::asio::awaitable<fcl::transport::chunk> read_transport_frame(fcl::transport::stream& stream,
                                                                   std::vector<std::uint8_t>& buffer,
                                                                   std::size_t& consumed,
                                                                   std::uint32_t max_frame_size) {
   while (true) {
      const auto decoded = fcl::transport::decode_frame_view(available_bytes(buffer, consumed),
                                                             fcl::transport::frame_options{.max_size = max_frame_size});
      if (decoded.status == fcl::transport::frame_decode_status::complete) {
         const auto payload = fcl::transport::chunk{decoded.payload};
         consumed += decoded.consumed;
         if (consumed >= buffer.size() || consumed > compact_threshold) {
            compact_buffer(buffer, consumed);
         }
         co_return payload;
      }

      compact_buffer(buffer, consumed);
      auto next = co_await stream.async_read_chunk();
      auto view = next.bytes();
      buffer.insert(buffer.end(), view.begin(), view.end());
   }
}

boost::asio::awaitable<void> write_transport_frame(fcl::transport::stream& stream, std::span<const std::uint8_t> payload,
                                                  std::uint32_t max_frame_size) {
   auto encoded = std::vector<std::uint8_t>{};
   fcl::transport::encode_frame_to(encoded, payload, fcl::transport::frame_options{.max_size = max_frame_size});
   co_await stream.async_write(fcl::transport::chunk{std::move(encoded)});
}

[[nodiscard]] bool is_clean_close(const fcl::exceptions::base& error) noexcept {
   return fcl::transport::exceptions::is(error, fcl::transport::exceptions::code::closed) ||
          fcl::transport::exceptions::is(error, fcl::transport::exceptions::code::canceled);
}

} // namespace

boost::asio::awaitable<void> serve_stream(fcl::transport::stream stream, fcl::api::binding_plan plan, options value) {
   co_await serve_stream(std::move(stream), std::move(plan), value, {});
}

boost::asio::awaitable<void> serve_stream(fcl::transport::stream stream, fcl::api::binding_plan plan, options value,
                                         fcl::api::metadata trusted_metadata) {
   auto dispatcher = fcl::api::frame_dispatcher{std::move(plan), fcl::api::dispatch_options{
                                                                     .codec = value.codec,
                                                                     .max_inflight = value.max_inflight,
                                                                     .deadline = value.deadline,
                                                                     .trusted_metadata = std::move(trusted_metadata),
                                                                  }};
   auto buffer = std::vector<std::uint8_t>{};
   auto consumed = std::size_t{0};

   while (true) {
      try {
         auto payload = co_await read_transport_frame(stream, buffer, consumed, value.max_frame_size);
         auto request = fcl::raw::unpack<fcl::api::frame>(payload.to_vector());
         auto responses = co_await dispatcher.dispatch(std::move(request));
         for (const auto& response : responses) {
            auto encoded = fcl::api::bytes{};
            fcl::raw::pack(encoded, response);
            co_await write_transport_frame(stream, encoded, value.max_frame_size);
         }
      } catch (const fcl::exceptions::base& error) {
         if (is_clean_close(error)) {
            co_return;
         }
         throw;
      }
   }
}

boost::asio::awaitable<void> serve_session(fcl::transport::session session, fcl::api::binding_plan plan,
                                           session_options value) {
   if (value.max_concurrent_streams == 0) {
      FCL_THROW_EXCEPTION(exceptions::resource_exhausted, "API transport max concurrent streams must be positive");
   }

   struct state {
      explicit state(boost::asio::any_io_executor executor)
          : strand(boost::asio::make_strand(std::move(executor))), wake(strand) {
         wake.expires_at(boost::asio::steady_timer::time_point::max());
      }

      boost::asio::strand<boost::asio::any_io_executor> strand;
      boost::asio::steady_timer wake;
      std::size_t slots = 0;
   };

   const auto executor = co_await boost::asio::this_coro::executor;
   auto shared = std::make_shared<state>(executor);
   auto reserve_slot = [shared, max = value.max_concurrent_streams]() -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(shared->strand, boost::asio::use_awaitable);
      while (shared->slots >= max) {
         shared->wake.expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await shared->wake.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
      ++shared->slots;
   };
   auto release_slot = [shared]() -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(shared->strand, boost::asio::use_awaitable);
      if (shared->slots > 0) {
         --shared->slots;
      }
      shared->wake.cancel();
   };
   auto wait_for_drain = [shared]() -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(shared->strand, boost::asio::use_awaitable);
      while (shared->slots > 0) {
         shared->wake.expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await shared->wake.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
   };

   auto accepting = true;
   while (accepting) {
      auto reserved = false;
      auto release_reserved = false;
      auto pending_error = std::exception_ptr{};
      try {
         co_await reserve_slot();
         reserved = true;
         auto stream = co_await session.async_accept_stream();
         boost::asio::co_spawn(
             executor,
             [release_slot, stream = std::move(stream), plan, stream_options = value.stream]() mutable
             -> boost::asio::awaitable<void> {
                try {
                   co_await serve_stream(std::move(stream), std::move(plan), stream_options);
                } catch (const fcl::exceptions::base&) {
                   // A bad API stream closes that stream; the session accept loop owns admission.
                } catch (...) {
                   // Detached stream failures must still release their reserved admission slot.
                }
                co_await release_slot();
             },
             boost::asio::detached);
      } catch (const fcl::exceptions::base& error) {
         if (reserved) {
            release_reserved = true;
         }
         if (is_clean_close(error)) {
            accepting = false;
         } else {
            pending_error = std::current_exception();
         }
      } catch (...) {
         if (reserved) {
            release_reserved = true;
         }
         pending_error = std::current_exception();
      }
      if (release_reserved) {
         co_await release_slot();
      }
      if (pending_error) {
         std::rethrow_exception(pending_error);
      }
   }
   co_await wait_for_drain();
}

} // namespace fcl::transport::api
