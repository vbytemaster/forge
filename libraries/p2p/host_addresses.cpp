module;

#include <optional>
#include <algorithm>
#include <boost/asio/ip/address.hpp>
#include <cctype>
#include <cstdint>
#include <string>
#include <set>
#include <utility>
#include <vector>

module fcl.p2p.node;

import fcl.p2p.endpoint;
import fcl.p2p.identity;

#include "host_addresses.hpp"

namespace fcl::p2p::host_addresses {
namespace {

enum class scope {
   public_address,
   private_address,
   loopback,
   link_local,
   unroutable,
   dns,
};

[[nodiscard]] std::string lower_host(std::string value) {
   std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
   while (!value.empty() && value.back() == '.') {
      value.pop_back();
   }
   return value;
}

[[nodiscard]] bool is_localhost_name(std::string value) {
   value = lower_host(std::move(value));
   return value == "localhost" || value.ends_with(".localhost");
}

[[nodiscard]] bool is_private_v4(const boost::asio::ip::address_v4& address) noexcept {
   const auto value = address.to_uint();
   return (value & 0xff00'0000U) == 0x0a00'0000U || (value & 0xfff0'0000U) == 0xac10'0000U ||
          (value & 0xffff'0000U) == 0xc0a8'0000U || (value & 0xffc0'0000U) == 0x6440'0000U;
}

[[nodiscard]] bool is_link_local_v4(const boost::asio::ip::address_v4& address) noexcept {
   return (address.to_uint() & 0xffff'0000U) == 0xa9fe'0000U;
}

[[nodiscard]] bool is_private_v6(const boost::asio::ip::address_v6& address) noexcept {
   const auto bytes = address.to_bytes();
   return (bytes[0] & 0xfeU) == 0xfcU;
}

[[nodiscard]] scope endpoint_scope(const endpoint& value) {
   if (value.relayed.has_value()) {
      return scope::public_address;
   }
   using host_kind = endpoint::host_kind;
   switch (value.transport.host_type) {
   case host_kind::dns:
   case host_kind::dns4:
   case host_kind::dns6:
      return is_localhost_name(value.transport.host) ? scope::loopback : scope::dns;
   case host_kind::ip4:
   case host_kind::ip6:
      break;
   }

   auto error = boost::system::error_code{};
   const auto parsed = boost::asio::ip::make_address(value.transport.host, error);
   if (error) {
      return scope::unroutable;
   }
   if (parsed.is_v4()) {
      const auto address = parsed.to_v4();
      if (address.is_loopback()) {
         return scope::loopback;
      }
      if (address.is_unspecified() || address.is_multicast()) {
         return scope::unroutable;
      }
      if (is_link_local_v4(address)) {
         return scope::link_local;
      }
      if (is_private_v4(address)) {
         return scope::private_address;
      }
      return scope::public_address;
   }

   const auto address = parsed.to_v6();
   if (address.is_loopback()) {
      return scope::loopback;
   }
   if (address.is_unspecified() || address.is_multicast()) {
      return scope::unroutable;
   }
   if (address.is_link_local()) {
      return scope::link_local;
   }
   if (is_private_v6(address)) {
      return scope::private_address;
   }
   return scope::public_address;
}

[[nodiscard]] bool peer_suffix_matches(const endpoint& value, const peer_id& peer) {
   if (value.relayed.has_value()) {
      return value.relayed->target.to_bytes() == peer.to_bytes();
   }
   return !value.peer.has_value() || value.peer->to_bytes() == peer.to_bytes();
}

[[nodiscard]] bool source_allows(scope candidate, const learning_context& context) {
   if (candidate == scope::dns || candidate == scope::public_address) {
      return true;
   }
   if (candidate == scope::link_local || candidate == scope::unroutable) {
      return false;
   }
   if (context.source == source_kind::third_party || !context.remote_endpoint.has_value()) {
      return false;
   }

   const auto remote = endpoint_scope(*context.remote_endpoint);
   if (remote == scope::loopback) {
      return candidate == scope::loopback || candidate == scope::private_address;
   }
   if (remote == scope::private_address) {
      return candidate == scope::private_address;
   }
   return false;
}

} // namespace

std::vector<endpoint> merge_advertised(const std::vector<endpoint>& configured, const std::vector<endpoint>& listened,
                                       const peer_id& local) {
   auto out = std::vector<endpoint>{};
   auto seen = std::set<std::string>{};
   const auto append = [&](endpoint value) {
      value.peer = local;
      const auto key = value.to_string();
      if (seen.insert(key).second) {
         out.push_back(std::move(value));
      }
   };
   for (const auto& endpoint : configured) {
      append(endpoint);
   }
   for (const auto& endpoint : listened) {
      append(endpoint);
   }
   return out;
}

std::optional<endpoint> learned(endpoint value, const peer_id& peer) {
   return learned(std::move(value), peer, learning_context{});
}

std::optional<endpoint> learned(endpoint value, const peer_id& peer, learning_context context) {
   if (!peer_suffix_matches(value, peer)) {
      return std::nullopt;
   }
   if (!source_allows(endpoint_scope(value), context)) {
      return std::nullopt;
   }
   if (!value.relayed.has_value()) {
      value.peer = peer;
   }
   return value;
}

} // namespace fcl::p2p::host_addresses
