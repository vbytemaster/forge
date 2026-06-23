module;

#include <algorithm>
#include <string>
#include <vector>

module forge.plugins.p2p.pubsub.plugin;

import forge.p2p.identity;
import forge.p2p.pubsub;
import forge.plugins.p2p.pubsub.types;

#include "details/message_projection.hxx"

namespace forge::plugins::p2p::pubsub {

bool contains_topic(const std::vector<std::string>& values, const std::string& topic) {
   return std::ranges::find(values, topic) != values.end();
}

message project_message(const forge::p2p::peer_id& source,
                        const forge::p2p::pubsub::message& value) {
   return message{
      .source = source,
      .author = value.from,
      .subject = value.subject,
      .data = value.data,
      .seqno = value.seqno,
   };
}

} // namespace forge::plugins::p2p::pubsub
