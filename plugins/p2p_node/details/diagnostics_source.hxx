#pragma once

namespace fcl::plugins::p2p_node {

class plugin::diagnostics_source_adapter final : public diagnostics_source {
 public:
   explicit diagnostics_source_adapter(std::shared_ptr<plugin::impl> impl);

   fcl::p2p::diagnostics::snapshot snapshot(fcl::p2p::diagnostics::options options) const override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::p2p_node
