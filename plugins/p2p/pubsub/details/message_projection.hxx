#pragma once

namespace forge::plugins::p2p::pubsub {

[[nodiscard]] bool contains_topic(const std::vector<std::string>& values, const std::string& topic);
[[nodiscard]] message project_message(const forge::p2p::peer_id& source,
                                      const forge::p2p::pubsub::message& value);

} // namespace forge::plugins::p2p::pubsub
