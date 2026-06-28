#pragma once

namespace forge::plugins::db::rocksdb::detail {

[[nodiscard]] std::vector<std::string> normalize_families(const std::vector<std::string>& requested);
[[nodiscard]] config decode_config(const forge::config::component_view& view);
void validate_config(const config& value);

} // namespace forge::plugins::db::rocksdb::detail
