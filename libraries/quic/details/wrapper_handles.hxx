#pragma once

#include "quic_engine.hxx"

#include <memory>

namespace forge::quic::detail {

struct connection_handle {
   std::shared_ptr<engine_connection> engine;
};

struct stream_handle {
   std::shared_ptr<engine_stream> engine;
};

} // namespace forge::quic::detail
