module;
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

export module fcl.multiformats.types;

export namespace fcl::multiformats {

using bytes = std::vector<std::uint8_t>;

class format_error final : public std::runtime_error {
 public:
   explicit format_error(std::string message) : std::runtime_error{std::move(message)} {}
};

} // namespace fcl::multiformats
