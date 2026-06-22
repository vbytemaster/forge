module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.plugins.p2p.pubsub.plugin;

import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p.pubsub;
import fcl.plugins.p2p.pubsub.exceptions;
import fcl.plugins.p2p.pubsub.types;

#include "details/config.hxx"

namespace fcl::plugins::p2p::pubsub {
namespace {

void validate_topic_list(const std::vector<std::string>& values, std::string_view name) {
   auto seen = std::set<std::string>{};
   for (const auto& value : values) {
      if (value.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "P2P PubSub topic policy contains empty topic",
                             fcl::exceptions::ctx("list", std::string{name}));
      }
      if (!seen.insert(value).second) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "P2P PubSub topic policy contains duplicate topic",
                             fcl::exceptions::ctx("list", std::string{name}), fcl::exceptions::ctx("topic", value));
      }
   }
}

} // namespace

std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config,
                          fcl::config::format_decode_diagnostics("invalid P2P PubSub config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   validate_topic_list(value.allowed_topics, "allowed-topics");
   validate_topic_list(value.denied_topics, "denied-topics");
}

fcl::p2p::pubsub::options core_options_for(const config& settings) {
   auto out = fcl::p2p::pubsub::options{};
   out.signatures =
      settings.sign_publishes ? fcl::p2p::pubsub::signature_policy::strict_sign
                              : fcl::p2p::pubsub::signature_policy::lax_no_sign;
   out.limits.max_data_size = static_cast<std::size_t>(settings.max_message_size);
   out.limits.max_message_size = static_cast<std::size_t>(settings.max_message_size) + 1024;
   out.limits.max_topics = static_cast<std::size_t>(settings.max_topics);
   out.limits.max_validation_queue = static_cast<std::size_t>(settings.max_active_handlers);
   return out;
}

} // namespace fcl::plugins::p2p::pubsub
