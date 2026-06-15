module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

export module fcl.api.context;

export import fcl.api.types;

export namespace fcl::api {

struct call_context {
   call_id id;
   api_ref api;
   std::string method;
   metadata meta;
   codec_id codec;
   frame_kind kind = frame_kind::request;
};

inline constexpr std::string_view trusted_metadata_prefix = "fcl.";
inline constexpr std::string_view p2p_remote_peer_metadata_key = "fcl.p2p.remote_peer";

class call_context_scope {
 public:
   explicit call_context_scope(call_context value);
   ~call_context_scope();

   call_context_scope(const call_context_scope&) = delete;
   call_context_scope& operator=(const call_context_scope&) = delete;

 private:
   bool active_ = true;
   std::thread::id owner_;
   std::uint64_t token_ = 0;
};

[[nodiscard]] std::optional<call_context> current_call_context();
[[nodiscard]] std::optional<std::string> metadata_value(const metadata& value, std::string_view key);

} // namespace fcl::api
