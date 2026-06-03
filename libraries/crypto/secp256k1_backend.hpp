#pragma once

/* private_key_impl based on libsecp256k1
 * used by secp256k1 key operations
 */

namespace fcl::crypto::secp256k1::detail {

const secp256k1_context* _get_context();

class private_key_impl {
 public:
   private_key_impl() noexcept;
   private_key_impl(const private_key_impl& cpy) noexcept;

   private_key_impl& operator=(const private_key_impl& pk) noexcept;

   private_key_secret _key;
};

} // namespace fcl::crypto::secp256k1::detail
