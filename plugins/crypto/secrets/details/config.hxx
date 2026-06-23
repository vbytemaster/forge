#pragma once

namespace forge::plugins::crypto::secrets {

[[nodiscard]] config decode_config(const forge::config::component_view& view);
void apply_config(plugin::impl& state, forge::config::component_view view);

} // namespace forge::plugins::crypto::secrets
