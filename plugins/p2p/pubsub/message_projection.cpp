module;

#include <algorithm>
#include <string>
#include <vector>

module fcl.plugins.p2p.pubsub.plugin;

import fcl.p2p.identity;
import fcl.p2p.pubsub;
import fcl.plugins.p2p.pubsub.types;

#include "details/message_projection.hxx"

namespace fcl::plugins::p2p::pubsub {

bool contains_topic(const std::vector<std::string>& values, const std::string& topic) {
   return std::ranges::find(values, topic) != values.end();
}

message project_message(const fcl::p2p::peer_id& source,
                        const fcl::p2p::pubsub::message& value) {
   return message{
      .source = source,
      .author = value.from,
      .subject = value.subject,
      .data = value.data,
      .seqno = value.seqno,
   };
}

} // namespace fcl::plugins::p2p::pubsub
