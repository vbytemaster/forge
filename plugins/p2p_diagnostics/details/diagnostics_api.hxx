#pragma once

namespace fcl::plugins::p2p_diagnostics {

class plugin::diagnostics_api final : public api {
 public:
   explicit diagnostics_api(std::shared_ptr<plugin::impl> impl);

   fcl::p2p::diagnostics::snapshot snapshot() const override;
   fcl::p2p::diagnostics::snapshot snapshot(fcl::p2p::diagnostics::options options) const override;
   fcl::p2p::diagnostics::network_state network() const override;
   fcl::p2p::resource_manager::snapshot resources() const override;
   fcl::p2p::pubsub::snapshot pubsub() const override;
   std::vector<fcl::p2p::diagnostics::peer> peers(filter value) const override;
   fcl::p2p::diagnostics::peer peer(fcl::p2p::peer_id value) const override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::p2p_diagnostics
