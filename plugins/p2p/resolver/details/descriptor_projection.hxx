#pragma once

namespace fcl::plugins::p2p::resolver {

inline constexpr auto resolver_api_id = "fcl.plugins.p2p.resolver.protocol";

[[nodiscard]] std::string api_key(const fcl::api::api_id& id, std::uint16_t major);
[[nodiscard]] entry project_descriptor(const fcl::api::descriptor& descriptor,
                                       const fcl::p2p::protocol_id& protocol,
                                       const fcl::transport::api::options& options);
void validate_entry(const entry& value, const config& limits, std::string_view source);
void validate_response(const std::vector<entry>& entries, const config& limits);
void validate_descriptor_compatible(const fcl::api::descriptor& descriptor, const entry& remote);
[[nodiscard]] std::optional<entry>
select_compatible(const std::vector<entry>& entries, const fcl::api::api_ref& requested);

} // namespace fcl::plugins::p2p::resolver
