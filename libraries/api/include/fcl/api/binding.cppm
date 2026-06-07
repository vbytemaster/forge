module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <typeinfo>
#include <utility>
#include <unordered_map>
#include <vector>

export module fcl.api.binding;

export import fcl.api.registry;

export namespace fcl::api {

enum class interceptor_phase : std::uint8_t {
   observe = 1,
   authorize = 2,
   limits = 3,
   before_call = 4,
   after_call = 5,
   error = 6,
};

struct call_context {
   call_id id;
   api_ref api;
   std::string method;
   metadata meta;
   codec_id codec;
   frame_kind kind = frame_kind::request;
};

using interceptor_handler = std::function<boost::asio::awaitable<void>(call_context&)>;

struct interceptor_step {
   std::string id;
   interceptor_phase phase = interceptor_phase::before_call;
   int order = 0;
   interceptor_handler handler;
};

class interceptor_builder {
 public:
   interceptor_builder& id(std::string value);
   interceptor_builder& phase(interceptor_phase value) noexcept;
   interceptor_builder& order(int value) noexcept;
   interceptor_builder& handler(interceptor_handler value);
   [[nodiscard]] interceptor_step build();

 private:
   interceptor_step value_;
};

[[nodiscard]] interceptor_builder interceptor();

struct call_runtime_options {
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
};

class call_runtime {
 public:
   explicit call_runtime(call_runtime_options options = {});

   void observe(const frame& value);
   [[nodiscard]] bool active(call_id id) const noexcept;
   [[nodiscard]] std::size_t active_calls() const noexcept;

 private:
   struct active_call {
      std::chrono::steady_clock::time_point started_at;
   };

   call_runtime_options options_;
   std::unordered_map<std::uint64_t, active_call> active_;
};

struct binding_plan {
   const registry* local = nullptr;
   std::vector<descriptor> exports;
   std::vector<api_ref> peer_requirements;
   std::vector<interceptor_step> interceptors;

   boost::asio::awaitable<frame> dispatch(frame request) const;
   boost::asio::awaitable<frame> dispatch(frame request, call_runtime& calls) const;
   boost::asio::awaitable<std::vector<frame>> dispatch_many(frame request) const;
   boost::asio::awaitable<std::vector<frame>> dispatch_many(frame request, call_runtime& calls) const;
   boost::asio::awaitable<std::vector<frame>> dispatch_stream(std::vector<frame> frames) const;
   boost::asio::awaitable<std::vector<frame>> dispatch_stream(std::vector<frame> frames, call_runtime& calls) const;
};

class binding_builder {
 public:
   binding_builder& serve(const registry& apis);
   binding_builder& serve(const view& apis);

   template <typename Interface> binding_builder& export_api(api_ref api) {
      static_assert(remote_interface<Interface>, "Interface must opt in to fcl::api::surface::remote");
      auto descriptor = Interface::describe();
      descriptor.id = std::move(api.id);
      descriptor.version.major = api.major;
      descriptor.version.revision = api.min_revision;
      plan_.exports.push_back(std::move(descriptor));
      return *this;
   }

   template <typename Interface> binding_builder& require_peer_api(api_ref api) {
      static_assert(remote_interface<Interface>, "Interface must opt in to fcl::api::surface::remote");
      plan_.peer_requirements.push_back(std::move(api));
      return *this;
   }

   binding_builder& interceptor(interceptor_step step);

   [[nodiscard]] binding_plan build();

 private:
   binding_plan plan_;
};

[[nodiscard]] binding_builder binding();

} // namespace fcl::api
