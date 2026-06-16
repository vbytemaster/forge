#pragma once

namespace fcl::plugins::p2p_api_resolver {

class plugin::resolver_api final : public api {
 public:
   explicit resolver_api(std::shared_ptr<plugin::impl> impl);

   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                    publish_options options) override;
   [[nodiscard]] std::vector<entry> local_apis() const override;
   boost::asio::awaitable<std::vector<entry>> peer_apis(fcl::p2p::peer_id peer,
                                                        resolve_options options) override;
   boost::asio::awaitable<resolution> resolve(fcl::p2p::peer_id peer, fcl::api::api_ref api,
                                              resolve_options options) override;

 private:
   boost::asio::awaitable<resolved_connection>
   open_resolved_connection(fcl::p2p::peer_id peer, fcl::api::api_ref api,
                            fcl::api::descriptor descriptor, resolve_options options) override;

   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::p2p_api_resolver
