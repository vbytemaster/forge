#pragma once

namespace forge::plugins::p2p::diagnostics {

class plugin::diagnostics_api final : public api {
 public:
   explicit diagnostics_api(std::shared_ptr<plugin::impl> impl);

   forge::p2p::diagnostics::snapshot snapshot() const override;
   forge::p2p::diagnostics::snapshot snapshot(forge::p2p::diagnostics::options options) const override;
   forge::p2p::diagnostics::network_state network() const override;
   forge::p2p::resource_manager::snapshot resources() const override;
   forge::p2p::pubsub::snapshot pubsub() const override;
   std::vector<forge::p2p::diagnostics::peer> peers(filter value) const override;
   forge::p2p::diagnostics::peer peer(forge::p2p::peer_id value) const override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace forge::plugins::p2p::diagnostics
