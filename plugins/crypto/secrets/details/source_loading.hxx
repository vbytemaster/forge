#pragma once

namespace forge::plugins::crypto::secrets {

inline void require_complete_file_read(std::istream& input, std::size_t expected_size, const std::string& path,
                                       const std::string& id) {
   const auto read_count = input.gcount();
   if (read_count < 0 || static_cast<std::size_t>(read_count) != expected_size || input.bad()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_source, "secret file read was incomplete",
                          forge::exceptions::ctx("secret_id", id),
                          forge::exceptions::ctx("path", path));
   }
}

[[nodiscard]] forge::crypto::secret_bytes load_secret_material(const secret_entry& entry,
                                                             std::uint64_t max_plaintext_bytes,
                                                             std::uint64_t max_ciphertext_bytes,
                                                             encrypted_file_decrypt_limits decrypt_limits);

} // namespace forge::plugins::crypto::secrets
