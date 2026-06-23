module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module forge.api.context;

export import forge.api.types;

export namespace forge::api {

struct call_context {
   call_id id;
   api_ref api;
   std::string method;
   metadata meta;
   bytes payload;
   codec_id codec;
   frame_kind kind = frame_kind::request;
};

inline constexpr std::string_view trusted_metadata_prefix = "forge.";
inline constexpr std::string_view p2p_remote_peer_metadata_key = "forge.p2p.remote_peer";

[[nodiscard]] std::optional<std::string> metadata_value(const metadata& value, std::string_view key);

} // namespace forge::api
