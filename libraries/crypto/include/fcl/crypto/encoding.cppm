module;
#include <string>
#include <string_view>

export module fcl.crypto.encoding;

import fcl.crypto.private_key;
import fcl.crypto.public_key;
import fcl.crypto.signature;

export namespace fcl::crypto {

class encoding {
 public:
   enum class kind {
      fcl,
      eos,
   };

   [[nodiscard]] static const encoding& fcl();
   [[nodiscard]] static const encoding& eos();

   [[nodiscard]] kind id() const noexcept;

   [[nodiscard]] public_key parse_public(std::string_view text) const;
   [[nodiscard]] private_key parse_private(std::string_view text) const;
   [[nodiscard]] signature parse_signature(std::string_view text) const;

   [[nodiscard]] std::string format(const public_key& key) const;
   [[nodiscard]] std::string format(const private_key& key) const;
   [[nodiscard]] std::string format(const signature& sig) const;

 private:
   explicit constexpr encoding(kind value) : kind_(value) {}

   kind kind_;
};

} // namespace fcl::crypto
