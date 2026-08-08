module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.api.transport.options;

export import forge.api.core.types;
export import forge.api.stream.options;

export namespace forge::api::transport {

using options = forge::api::stream::options;

using call_options = forge::api::stream::call_options;

struct session_options {
   options stream;
   std::size_t max_concurrent_streams = 128;
};

} // namespace forge::api::transport
