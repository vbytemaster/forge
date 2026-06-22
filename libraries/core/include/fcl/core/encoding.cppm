module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module fcl.core.encoding;

export namespace fcl::encoding {

[[nodiscard]] std::string to_base64(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> from_base64(std::string_view input);

[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> data);
std::size_t from_hex(std::string_view input, std::span<std::uint8_t> output);

} // namespace fcl::encoding
