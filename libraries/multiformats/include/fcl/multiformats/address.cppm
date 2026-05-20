module;
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module fcl.multiformats.address;

import fcl.multiformats.types;
import fcl.multiformats.multicodec;

export namespace fcl::multiformats {

class address {
 public:
   struct component {
      multicodec_code code;
      std::string value;
   };

   [[nodiscard]] static address parse(std::string_view value);
   [[nodiscard]] static address from_bytes(std::span<const std::uint8_t> data);

   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] bytes to_bytes() const;
   [[nodiscard]] const std::vector<component>& components() const noexcept;

   void push(component value);

 private:
   std::vector<component> components_;
};

} // namespace fcl::multiformats
