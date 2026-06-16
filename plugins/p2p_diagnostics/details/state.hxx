#pragma once

namespace fcl::plugins::p2p_diagnostics {

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   config settings;
   std::shared_ptr<fcl::plugins::p2p_node::diagnostics_source> source;
   bool initialized = false;
   bool stopping = false;

   [[nodiscard]] fcl::plugins::p2p_node::diagnostics_source& require_source() const;
   [[nodiscard]] fcl::p2p::diagnostics::snapshot snapshot() const;
};

} // namespace fcl::plugins::p2p_diagnostics
