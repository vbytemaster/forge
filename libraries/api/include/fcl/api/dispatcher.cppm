module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

export module fcl.api.dispatcher;

export import fcl.api.binding;

export namespace fcl::api {

struct dispatch_options {
   codec_id codec{.value = "fcl.raw"};
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

   boost::asio::awaitable<std::vector<frame>> dispatch(frame value);

   [[nodiscard]] const dispatch_options& options() const noexcept;
   [[nodiscard]] std::size_t active_calls() const noexcept;
   [[nodiscard]] std::size_t grouped_calls() const noexcept;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::api
