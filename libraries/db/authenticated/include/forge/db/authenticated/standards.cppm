module;

#include <span>
#include <string_view>

export module forge.db.authenticated.standards;

import forge.db.authenticated.types;

export namespace forge::db::authenticated {

enum class proof_standard {
   forge_v3,
   cosmos_ics23_v1,
};

struct standard_support {
   bool native_codec = false;
   bool native_verifier = false;
   std::string_view required_dependency;

   bool operator==(const standard_support&) const = default;
};

[[nodiscard]] constexpr standard_support support_for(proof_standard standard) noexcept {
   switch (standard) {
   case proof_standard::forge_v3:
      return {
          .native_codec = true,
          .native_verifier = true,
      };
   case proof_standard::cosmos_ics23_v1:
      return {
          .required_dependency = "official cosmos/ics23 protobuf implementation",
      };
   }
   return {};
}

class point_proof_standard_adapter {
 public:
   virtual ~point_proof_standard_adapter() = default;

   [[nodiscard]] virtual proof_standard standard() const noexcept = 0;
   [[nodiscard]] virtual bytes encode_point(const point_proof& proof) const = 0;
   [[nodiscard]] virtual verified_point verify_point(std::string_view domain, const root& expected_anchor,
                                                     std::span<const std::byte> expected_key,
                                                     std::span<const std::byte> encoded_proof,
                                                     const limits& settings = {}) const = 0;
};

} // namespace forge::db::authenticated
