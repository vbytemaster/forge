module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module fcl.api.context;

export import fcl.api.types;

export namespace fcl::api {

struct call_context {
   call_id id;
   api_ref api;
   std::string method;
   metadata meta;
   bytes payload;
   codec_id codec;
   frame_kind kind = frame_kind::request;
};

inline constexpr std::string_view trusted_metadata_prefix = "fcl.";
inline constexpr std::string_view p2p_remote_peer_metadata_key = "fcl.p2p.remote_peer";

[[nodiscard]] std::optional<std::string> metadata_value(const metadata& value, std::string_view key);

} // namespace fcl::api
