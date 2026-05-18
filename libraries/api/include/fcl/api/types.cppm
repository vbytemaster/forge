module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module fcl.api.types;

export namespace fcl::api {

using bytes = std::vector<std::uint8_t>;

struct metadata_entry {
   std::string key;
   std::string value;

   bool operator==(const metadata_entry&) const = default;
};

using metadata = std::vector<metadata_entry>;

struct api_id {
   std::string value;

   bool operator==(const api_id&) const = default;
};

struct api_version {
   std::uint16_t major = 1;
   std::uint16_t revision = 0;

   bool operator==(const api_version&) const = default;
};

struct api_ref {
   api_id id;
   std::uint16_t major = 1;
   std::uint16_t min_revision = 0;

   bool operator==(const api_ref&) const = default;
};

struct codec_id {
   std::string value;

   bool operator==(const codec_id&) const = default;
};

struct call_id {
   std::uint64_t value = 0;

   bool operator==(const call_id&) const = default;
};

enum class frame_kind : std::uint8_t {
   request = 1,
   response = 2,
   error = 3,
   cancel = 4,
   stream_item = 5,
   stream_end = 6,
};

enum class method_kind : std::uint8_t {
   unary = 1,
   server_stream = 2,
   client_stream = 3,
   bidirectional_stream = 4,
};

enum class status : std::uint16_t {
   ok = 0,
   invalid_argument = 400,
   unauthenticated = 401,
   permission_denied = 403,
   not_found = 404,
   conflict = 409,
   failed_precondition = 412,
   resource_exhausted = 429,
   deadline_exceeded = 504,
   unavailable = 503,
   internal = 500,
};

enum class error_kind : std::uint8_t {
   api = 1,
   invalid_request = 2,
   codec = 3,
   compatibility = 4,
   timeout = 5,
   cancelled = 6,
   transport = 7,
   internal = 8,
};

struct error_identity {
   std::string category;
   std::uint32_t code = 0;

   bool operator==(const error_identity&) const = default;
};

struct error_payload {
   std::string error;
   std::string message;
   bool retryable = false;
   status status_code = status::internal;
   error_identity identity;
   std::optional<codec_id> details_codec;
   std::optional<bytes> details;

   bool operator==(const error_payload&) const = default;
};

struct frame {
   frame_kind kind = frame_kind::request;
   call_id id;
   api_ref api;
   std::string method;
   metadata meta;
   codec_id codec;
   bytes payload;

   bool operator==(const frame&) const = default;
};

BOOST_DESCRIBE_ENUM(frame_kind, request, response, error, cancel, stream_item, stream_end)
BOOST_DESCRIBE_ENUM(method_kind, unary, server_stream, client_stream, bidirectional_stream)
BOOST_DESCRIBE_ENUM(status, ok, invalid_argument, unauthenticated, permission_denied, not_found, conflict,
                    failed_precondition, resource_exhausted, deadline_exceeded, unavailable, internal)
BOOST_DESCRIBE_ENUM(error_kind, api, invalid_request, codec, compatibility, timeout, cancelled, transport, internal)
BOOST_DESCRIBE_STRUCT(api_id, (), (value))
BOOST_DESCRIBE_STRUCT(api_version, (), (major, revision))
BOOST_DESCRIBE_STRUCT(api_ref, (), (id, major, min_revision))
BOOST_DESCRIBE_STRUCT(codec_id, (), (value))
BOOST_DESCRIBE_STRUCT(call_id, (), (value))
BOOST_DESCRIBE_STRUCT(metadata_entry, (), (key, value))
BOOST_DESCRIBE_STRUCT(error_identity, (), (category, code))
BOOST_DESCRIBE_STRUCT(error_payload, (), (error, message, retryable, status_code, identity, details_codec, details))
BOOST_DESCRIBE_STRUCT(frame, (), (kind, id, api, method, meta, codec, payload))

} // namespace fcl::api
