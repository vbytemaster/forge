#pragma once

namespace fcl::plugins::signature_provider {

[[nodiscard]] config decode_config(const fcl::config::component_view& view);
void apply_config(plugin::impl& state, fcl::config::component_view view);

} // namespace fcl::plugins::signature_provider
