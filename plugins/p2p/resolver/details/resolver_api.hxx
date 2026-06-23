#pragma once

namespace forge::plugins::p2p::resolver {

class plugin::resolver_api final : public api {
 public:
   explicit resolver_api(std::shared_ptr<plugin::impl> impl);

   void publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol,
                    publish_options options) override;
   [[nodiscard]] std::vector<entry> local_apis() const override;
   boost::asio::awaitable<std::vector<entry>> peer_apis(forge::p2p::peer_id peer,
                                                        resolve_options options) override;
   boost::asio::awaitable<resolution> resolve(forge::p2p::peer_id peer, forge::api::api_ref api,
                                              resolve_options options) override;

 private:
   boost::asio::awaitable<resolved_connection>
   open_resolved_connection(forge::p2p::peer_id peer, forge::api::api_ref api,
                            forge::api::descriptor descriptor, resolve_options options) override;

   std::shared_ptr<plugin::impl> impl_;
};

} // namespace forge::plugins::p2p::resolver
