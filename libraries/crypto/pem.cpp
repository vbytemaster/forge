module;

#include <fcl/exception/macros.hpp>

#include <openssl/bio.h>
#include <openssl/pem.h>

#include <memory>
#include <string>

module fcl.crypto.pem;

import fcl.crypto.asymmetric;
import fcl.crypto.der;

namespace fcl::crypto::pem {
using asymmetric::private_key;
using asymmetric::public_key;

namespace {

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

struct pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

using bio_ptr = std::unique_ptr<BIO, bio_deleter>;
using pkey_ptr = std::unique_ptr<EVP_PKEY, pkey_deleter>;

[[nodiscard]] bio_ptr memory_bio(std::string_view text) {
   auto out = bio_ptr{BIO_new_mem_buf(text.data(), static_cast<int>(text.size()))};
   if (!out) {
      FCL_THROW_EXCEPTION(exceptions::backend_error, "failed to allocate PEM BIO");
   }
   return out;
}

[[nodiscard]] bytes write_private_der(EVP_PKEY* key) {
   const auto length = i2d_PrivateKey(key, nullptr);
   if (length <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to size private key DER");
   }
   auto out = bytes(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PrivateKey(key, &cursor) != length) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to write private key DER");
   }
   return out;
}

[[nodiscard]] bytes write_public_der(EVP_PKEY* key) {
   const auto length = i2d_PUBKEY(key, nullptr);
   if (length <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to size public key DER");
   }
   auto out = bytes(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key, &cursor) != length) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to write public key DER");
   }
   return out;
}

} // namespace

bytes read_block(std::string_view text, std::string_view label) {
   auto bio = memory_bio(text);
   char* name = nullptr;
   char* header = nullptr;
   unsigned char* data = nullptr;
   long size = 0;
   if (PEM_read_bio(bio.get(), &name, &header, &data, &size) != 1 || name == nullptr || data == nullptr || size <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to read PEM block");
   }
   auto cleanup = [&] {
      OPENSSL_free(name);
      OPENSSL_free(header);
      OPENSSL_free(data);
   };
   const auto block_name = std::string_view{name};
   if (!label.empty() && block_name != label) {
      cleanup();
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "PEM block label mismatch");
   }
   auto out = bytes(data, data + size);
   cleanup();
   return out;
}

private_key read_private_key(std::string_view text) {
   auto bio = memory_bio(text);
   auto key = pkey_ptr{PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr)};
   if (!key) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to parse private key PEM");
   }
   return der::read_private_key(write_private_der(key.get()));
}

public_key read_public_key(std::string_view text) {
   auto bio = memory_bio(text);
   auto key = pkey_ptr{PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr)};
   if (!key) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "failed to parse public key PEM");
   }
   return der::read_public_key(write_public_der(key.get()));
}

} // namespace fcl::crypto::pem
