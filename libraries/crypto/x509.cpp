module;

#include <forge/exceptions/macros.hpp>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

module forge.crypto.x509;

import forge.crypto.asymmetric;
import forge.crypto.der;
import forge.crypto.hex;
import forge.crypto.sha256;

namespace forge::crypto::x509 {
namespace {

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

struct asn1_object_deleter {
   void operator()(ASN1_OBJECT* value) const noexcept {
      ASN1_OBJECT_free(value);
   }
};

using bio_ptr = std::unique_ptr<BIO, bio_deleter>;
using x509_ptr = std::unique_ptr<X509, x509_deleter>;
using pkey_ptr = std::unique_ptr<EVP_PKEY, pkey_deleter>;
using asn1_object_ptr = std::unique_ptr<ASN1_OBJECT, asn1_object_deleter>;

[[nodiscard]] x509_ptr parse_der(std::span<const std::uint8_t> bytes) {
   const auto* cursor = bytes.data();
   auto out = x509_ptr{d2i_X509(nullptr, &cursor, static_cast<long>(bytes.size()))};
   if (!out) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "failed to parse X.509 certificate DER");
   }
   return out;
}

[[nodiscard]] bytes write_der(X509* certificate) {
   const auto length = i2d_X509(certificate, nullptr);
   if (length <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "failed to size X.509 certificate DER");
   }
   auto out = bytes(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_X509(certificate, &cursor) != length) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "failed to write X.509 certificate DER");
   }
   return out;
}

} // namespace

certificate::certificate(bytes der) : der_(std::move(der)) {}

certificate certificate::from_der(std::span<const std::uint8_t> bytes_value) {
   auto parsed = parse_der(bytes_value);
   return certificate{write_der(parsed.get())};
}

certificate certificate::from_pem(std::string_view text) {
   auto bio = bio_ptr{BIO_new_mem_buf(text.data(), static_cast<int>(text.size()))};
   if (!bio) {
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "failed to allocate X.509 PEM BIO");
   }
   auto parsed = x509_ptr{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
   if (!parsed) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "failed to parse X.509 certificate PEM");
   }
   return certificate{write_der(parsed.get())};
}

const bytes& certificate::der() const noexcept {
   return der_;
}

bytes certificate::public_key_der() const {
   auto parsed = parse_der(der_);
   auto key = pkey_ptr{X509_get_pubkey(parsed.get())};
   if (!key) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "X.509 certificate has no public key");
   }
   const auto length = i2d_PUBKEY(key.get(), nullptr);
   if (length <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "failed to size certificate public key DER");
   }
   auto out = bytes(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key.get(), &cursor) != length) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "failed to write certificate public key DER");
   }
   return out;
}

asymmetric::public_key certificate::key() const {
   return der::read_public_key(public_key_der());
}

bytes certificate::extension(std::string_view oid) const {
   auto parsed = parse_der(der_);
   auto object = asn1_object_ptr{OBJ_txt2obj(std::string{oid}.c_str(), 1)};
   if (!object) {
      return {};
   }
   const auto index = X509_get_ext_by_OBJ(parsed.get(), object.get(), -1);
   if (index < 0) {
      return {};
   }
   auto* extension_value = X509_get_ext(parsed.get(), index);
   const auto* data = extension_value == nullptr ? nullptr : X509_EXTENSION_get_data(extension_value);
   if (data == nullptr || data->data == nullptr || data->length <= 0) {
      return {};
   }
   return bytes(data->data, data->data + data->length);
}

bytes certificate::fingerprint_sha256() const {
   const auto digest = sha256::hash(der_).to_uint8_span();
   return bytes(digest.begin(), digest.end());
}

std::string certificate::fingerprint_sha256_text() const {
   return forge::crypto::to_hex(fingerprint_sha256());
}

} // namespace forge::crypto::x509
