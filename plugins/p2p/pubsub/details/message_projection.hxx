#pragma once

namespace fcl::plugins::p2p::pubsub {

[[nodiscard]] bool contains_topic(const std::vector<std::string>& values, const std::string& topic);
[[nodiscard]] message project_message(const fcl::p2p::peer_id& source,
                                      const fcl::p2p::pubsub::message& value);

} // namespace fcl::plugins::p2p::pubsub
