module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <source_location>
#include <string>
#include <string_view>

module forge.plugins.crypto.secrets.plugin;

import forge.crypto.base64;
import forge.crypto.hex;
import forge.crypto.secret_bytes;
import forge.crypto.types;
import forge.exceptions;
import forge.plugins.crypto.secrets.exceptions;
import forge.plugins.crypto.secrets.types;

#include "details/source_loading.hxx"

namespace forge::plugins::crypto::secrets {
namespace {

[[nodiscard]] std::string trim_ascii(std::string value) {
   auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
   value.erase(value.begin(), std::ranges::find_if(value, not_space));
   value.erase(std::ranges::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
   return value;
}

[[nodiscard]] forge::crypto::bytes bytes_from_string(std::string_view value) {
   return forge::crypto::bytes{value.begin(), value.end()};
}

[[nodiscard]] forge::crypto::bytes decode_material(std::string value, encoding encoding_value, const std::string& id) {
   try {
      switch (encoding_value) {
      case encoding::raw:
         return bytes_from_string(value);
      case encoding::hex: {
         value = trim_ascii(std::move(value));
         if ((value.size() % 2U) != 0U) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "hex secret has odd length",
                                forge::exceptions::ctx("secret_id", id));
         }
         auto output = forge::crypto::bytes(value.size() / 2U);
         const auto written = forge::crypto::from_hex(value, output.data(), output.size());
         output.resize(written);
         return output;
      }
      case encoding::base64: {
         value = trim_ascii(std::move(value));
         auto decoded = forge::crypto::base64_decode<std::vector<char>>(value);
         return forge::crypto::bytes{decoded.begin(), decoded.end()};
      }
      }
   } catch (const exceptions::invalid_secret&) {
      throw;
   } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "secret material cannot be decoded",
                          forge::exceptions::ctx("secret_id", id));
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "unknown secret encoding", forge::exceptions::ctx("secret_id", id));
}

[[nodiscard]] forge::crypto::bytes read_file(const std::string& path, std::uint64_t max_bytes, const std::string& id) {
   if (path.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_source, "secret file path is empty",
                          forge::exceptions::ctx("secret_id", id));
   }
   auto in = std::ifstream{std::filesystem::path{path}, std::ios::binary};
   if (!in) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_source, "secret file cannot be opened",
                          forge::exceptions::ctx("secret_id", id),
                          forge::exceptions::ctx("path", path));
   }
   in.seekg(0, std::ios::end);
   const auto size = in.tellg();
   if (size < 0 || static_cast<std::uint64_t>(size) > max_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::size_limit_exceeded, "secret file exceeds configured limit",
                          forge::exceptions::ctx("secret_id", id));
   }
   in.seekg(0, std::ios::beg);
   auto output = forge::crypto::bytes(static_cast<std::size_t>(size));
   if (!output.empty()) {
      in.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
      require_complete_file_read(in, output.size(), path, id);
   }
   return output;
}

[[nodiscard]] std::string read_passphrase(const secret_source& source, const std::string& id) {
   if (!source.passphrase_value.empty()) {
      return source.passphrase_value;
   }
   if (!source.passphrase_file.empty()) {
      const auto bytes = read_file(source.passphrase_file, 64U * 1024U, id);
      return trim_ascii(std::string{bytes.begin(), bytes.end()});
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_source, "encrypted_file source requires passphrase",
                       forge::exceptions::ctx("secret_id", id));
}

} // namespace

forge::crypto::secret_bytes load_secret_material(const secret_entry& entry,
                                               std::uint64_t max_plaintext_bytes,
                                               std::uint64_t max_ciphertext_bytes,
                                               encrypted_file_decrypt_limits decrypt_limits) {
   auto material = forge::crypto::bytes{};
   switch (entry.source.type) {
   case source_type::value:
      material = decode_material(entry.source.value, entry.source.encoding, entry.id);
      break;
   case source_type::file: {
      auto bytes = read_file(entry.source.path, max_ciphertext_bytes, entry.id);
      material = decode_material(std::string{bytes.begin(), bytes.end()}, entry.source.encoding, entry.id);
      break;
   }
   case source_type::encrypted_file: {
      auto container = read_file(entry.source.path, max_ciphertext_bytes, entry.id);
      auto passphrase = read_passphrase(entry.source, entry.id);
      try {
         decrypt_limits.max_plaintext_bytes = max_plaintext_bytes;
         material = decrypt_secret_file(container, passphrase, decrypt_limits);
      } catch (const exceptions::size_limit_exceeded&) {
         forge::exceptions::capture_and_rethrow("encrypted secret file source",
                                              std::source_location::current(),
                                              forge::exceptions::ctx("secret_id", entry.id));
      } catch (const exceptions::invalid_secret&) {
         forge::exceptions::capture_and_rethrow("encrypted secret file source",
                                              std::source_location::current(),
                                              forge::exceptions::ctx("secret_id", entry.id));
      } catch (const std::exception&) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "encrypted secret file cannot be decrypted",
                             forge::exceptions::ctx("secret_id", entry.id));
      }
      break;
   }
   }

   if (material.size() > max_plaintext_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::size_limit_exceeded, "secret material exceeds configured plaintext limit",
                          forge::exceptions::ctx("secret_id", entry.id));
   }
   if (material.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "secret material is empty",
                          forge::exceptions::ctx("secret_id", entry.id));
   }
   return forge::crypto::secret_bytes{std::move(material)};
}

} // namespace forge::plugins::crypto::secrets
