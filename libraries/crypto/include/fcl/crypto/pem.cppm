module;

#include <string_view>

export module fcl.crypto.pem;

import fcl.crypto.asymmetric;
import fcl.crypto.types;

export namespace fcl::crypto::pem {

[[nodiscard]] bytes read_block(std::string_view text, std::string_view label);
[[nodiscard]] asymmetric::private_key read_private_key(std::string_view text);
[[nodiscard]] asymmetric::public_key read_public_key(std::string_view text);

} // namespace fcl::crypto::pem
