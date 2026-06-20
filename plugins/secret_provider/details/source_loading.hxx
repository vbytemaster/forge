#pragma once

namespace fcl::plugins::secret_provider {

[[nodiscard]] fcl::crypto::secret_bytes load_secret_material(const secret_entry& entry,
                                                             std::uint64_t max_plaintext_bytes,
                                                             std::uint64_t max_ciphertext_bytes,
                                                             encrypted_file_decrypt_limits decrypt_limits);

} // namespace fcl::plugins::secret_provider
