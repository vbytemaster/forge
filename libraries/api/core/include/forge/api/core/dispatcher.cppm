module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

export module forge.api.core.dispatcher;

export import forge.api.core.binding;

export namespace forge::api::core {

struct dispatch_options {
   codec_id codec{.value = "forge.raw"};
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
   metadata trusted_metadata;
};

class frame_dispatcher {
 public:
   frame_dispatcher(binding_plan plan, dispatch_options options = {});
   ~frame_dispatcher();

   frame_dispatcher(frame_dispatcher&&) noexcept;
   frame_dispatcher& operator=(frame_dispatcher&&) noexcept;

   frame_dispatcher(const frame_dispatcher&) = delete;
   frame_dispatcher& operator=(const frame_dispatcher&) = delete;

   boost::asio::awaitable<frame> dispatch(frame value);
   boost::asio::awaitable<frame>
   dispatch_stream(frame value, std::shared_ptr<detail::stream_endpoint> input,
                   std::shared_ptr<detail::stream_endpoint> output);

   [[nodiscard]] const dispatch_options& options() const noexcept;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::api::core
