#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.crypto.asymmetric;
import fcl.crypto.der;
import fcl.crypto.p256;
import fcl.crypto.pem;
import fcl.crypto.rsa;
import fcl.crypto.secp256k1;
import fcl.crypto.x509;
import fcl.p2p.dht;
import fcl.p2p.discovery;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.exceptions;
import fcl.p2p.hole_punch;
import fcl.p2p.identify;
import fcl.p2p.identity;
import fcl.p2p.message;
import fcl.p2p.negotiation;
import fcl.p2p.node;
import fcl.p2p.peer_store;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.reachability;
import fcl.p2p.rendezvous;
import fcl.p2p.relay;
import fcl.p2p.resource_manager;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.quic.endpoint;
import fcl.quic.libp2p;
import fcl.quic.transport;
import fcl.transport.endpoint;
import fcl.transport.frame;
import fcl.transport.stream;
import fcl.tcp.listener;
import fcl.multiformats;

#include "../../libraries/p2p/session_lifecycle.hpp"
#include "../../libraries/p2p/relay_accounting.hpp"

namespace fcl::p2p {
namespace {

struct product_announce {
   std::string ref;

   bool operator==(const product_announce&) const = default;
};

BOOST_DESCRIBE_STRUCT(product_announce, (), (ref))

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

struct evp_pkey_ctx_deleter {
   void operator()(EVP_PKEY_CTX* value) const noexcept {
      EVP_PKEY_CTX_free(value);
   }
};

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct asn1_object_deleter {
   void operator()(ASN1_OBJECT* value) const noexcept {
      ASN1_OBJECT_free(value);
   }
};

struct asn1_octet_string_deleter {
   void operator()(ASN1_OCTET_STRING* value) const noexcept {
      ASN1_OCTET_STRING_free(value);
   }
};

struct x509_extension_deleter {
   void operator()(X509_EXTENSION* value) const noexcept {
      X509_EXTENSION_free(value);
   }
};

struct test_identity {
   public_key key;
   fcl::crypto::asymmetric::private_key private_key;
   std::string private_key_pem;
   peer_id peer;
};

struct test_certificate_identity {
   std::string certificate_pem;
   std::string private_key_pem;
   peer_id peer;
};

std::string bio_to_string(BIO* value) {
   BUF_MEM* buffer = nullptr;
   BIO_get_mem_ptr(value, &buffer);
   if (buffer == nullptr || buffer->data == nullptr) {
      throw std::runtime_error{"failed to read BIO buffer"};
   }
   return {buffer->data, buffer->length};
}

test_identity make_test_identity() {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")};
   if (!key) {
      throw std::runtime_error{"failed to generate Ed25519 identity"};
   }

   auto public_size = std::size_t{};
   if (EVP_PKEY_get_raw_public_key(key.get(), nullptr, &public_size) != 1 || public_size == 0) {
      throw std::runtime_error{"failed to size Ed25519 public key"};
   }
   auto public_bytes = std::vector<std::uint8_t>(public_size);
   if (EVP_PKEY_get_raw_public_key(key.get(), public_bytes.data(), &public_size) != 1) {
      throw std::runtime_error{"failed to read Ed25519 public key"};
   }
   public_bytes.resize(public_size);

   auto private_key_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   if (!private_key_bio ||
       PEM_write_bio_PrivateKey(private_key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      throw std::runtime_error{"failed to write Ed25519 private key PEM"};
   }

   const auto private_key_pem = bio_to_string(private_key_bio.get());
   auto out = test_identity{
       .key = public_key{.type = public_key::type::ed25519, .data = std::move(public_bytes)},
       .private_key = fcl::crypto::pem::read_private_key(private_key_pem),
       .private_key_pem = private_key_pem,
   };
   out.peer = make_peer_id(out.key);
   return out;
}

template <typename Range> std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

std::vector<std::uint8_t> certificate_public_key_der(X509* certificate);
std::vector<std::uint8_t> signed_key_der(std::span<const std::uint8_t> public_key,
                                         std::span<const std::uint8_t> signature);
std::vector<std::uint8_t> tls_identity_message(std::span<const std::uint8_t> certificate_public_key);

test_identity make_secp256k1_identity() {
   auto private_key = fcl::crypto::asymmetric::private_key::generate<fcl::crypto::secp256k1::private_key_shim>();
   auto key = public_key{
       .type = public_key::type::secp256k1,
       .data = bytes_from_range(private_key.get_public_key().as<fcl::crypto::secp256k1::public_key_shim>().serialize()),
   };
   auto out = test_identity{.key = std::move(key), .private_key = private_key};
   out.peer = make_peer_id(out.key);
   return out;
}

test_identity make_p256_identity() {
   auto private_key = fcl::crypto::asymmetric::private_key::generate_p256<fcl::crypto::p256::private_key_shim>();
   auto key = public_key{
       .type = public_key::type::ecdsa,
       .data = fcl::crypto::der::write_public_key(private_key.get_public_key()),
   };
   auto out = test_identity{.key = std::move(key), .private_key = private_key};
   out.peer = make_peer_id(out.key);
   return out;
}

test_identity make_rsa_identity() {
   auto private_key = fcl::crypto::asymmetric::private_key::generate<fcl::crypto::rsa::private_key_shim>();
   auto key = public_key{
       .type = public_key::type::rsa,
       .data = private_key.get_public_key().as<fcl::crypto::rsa::public_key_shim>().serialize(),
   };
   auto out = test_identity{.key = std::move(key), .private_key = private_key};
   out.peer = make_peer_id(out.key);
   return out;
}

std::vector<std::uint8_t> make_signed_rendezvous_peer_record(const test_identity& identity,
                                                             std::vector<endpoint> endpoints = {},
                                                             std::uint64_t sequence = 1) {
   if (endpoints.empty()) {
      endpoints.push_back(parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + identity.peer.to_string()));
   }
   return rendezvous::codec::seal_peer_record(
              rendezvous::peer_record{
                  .peer = identity.peer,
                  .endpoints = std::move(endpoints),
                  .sequence = sequence,
              },
              identity.key, fcl::crypto::pem::read_private_key(identity.private_key_pem))
       .encode();
}

std::vector<std::uint8_t> make_signed_rendezvous_peer_record(const test_certificate_identity& identity,
                                                             std::vector<endpoint> endpoints = {},
                                                             std::uint64_t sequence = 1) {
   const auto private_key = fcl::crypto::pem::read_private_key(identity.private_key_pem);
   const auto key = public_key{
       .type = public_key::type::rsa,
       .data = fcl::crypto::der::write_public_key(private_key.get_public_key()),
   };
   if (endpoints.empty()) {
      endpoints.push_back(parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + identity.peer.to_string()));
   }
   return rendezvous::codec::seal_peer_record(
              rendezvous::peer_record{
                  .peer = identity.peer,
                  .endpoints = std::move(endpoints),
                  .sequence = sequence,
              },
              key, private_key)
       .encode();
}

test_certificate_identity make_test_certificate_identity(std::string_view common_name) {
   auto key_context =
       std::unique_ptr<EVP_PKEY_CTX, evp_pkey_ctx_deleter>{EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr)};
   if (!key_context || EVP_PKEY_keygen_init(key_context.get()) != 1 ||
       EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), 2048) != 1) {
      throw std::runtime_error{"failed to initialize test RSA key generation"};
   }

   EVP_PKEY* raw_key = nullptr;
   if (EVP_PKEY_keygen(key_context.get(), &raw_key) != 1 || raw_key == nullptr) {
      throw std::runtime_error{"failed to generate test RSA key"};
   }
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{raw_key};
   auto private_key_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   if (!private_key_bio ||
       PEM_write_bio_PrivateKey(private_key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      throw std::runtime_error{"failed to write test private key PEM"};
   }
   const auto private_key_pem = bio_to_string(private_key_bio.get());

   auto certificate = std::unique_ptr<X509, x509_deleter>{X509_new()};
   if (!certificate) {
      throw std::runtime_error{"failed to allocate test certificate"};
   }
   if (X509_set_version(certificate.get(), 2) != 1 ||
       ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()),
                        static_cast<long>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff)) !=
           1 ||
       X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) == nullptr ||
       X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 24 * 60 * 60) == nullptr ||
       X509_set_pubkey(certificate.get(), key.get()) != 1) {
      throw std::runtime_error{"failed to configure test certificate"};
   }
   auto* name = X509_get_subject_name(certificate.get());
   if (name == nullptr ||
       X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name.data()),
                                  static_cast<int>(common_name.size()), -1, 0) != 1 ||
       X509_set_issuer_name(certificate.get(), name) != 1) {
      throw std::runtime_error{"failed to configure test certificate subject"};
   }

   const auto identity_private_key = fcl::crypto::pem::read_private_key(private_key_pem);
   const auto identity_key = public_key{
       .type = public_key::type::rsa,
       .data = fcl::crypto::der::write_public_key(identity_private_key.get_public_key()),
   };
   const auto spki = certificate_public_key_der(certificate.get());
   const auto signature = identity_private_key.sign(tls_identity_message(spki)).visit([](const auto& value) {
      return bytes_from_range(value.serialize());
   });
   const auto extension_value = signed_key_der(encode_public_key(identity_key), signature);
   auto object = std::unique_ptr<ASN1_OBJECT, asn1_object_deleter>{OBJ_txt2obj("1.3.6.1.4.1.53594.1.1", 1)};
   auto octets = std::unique_ptr<ASN1_OCTET_STRING, asn1_octet_string_deleter>{ASN1_OCTET_STRING_new()};
   if (!object || !octets ||
       ASN1_OCTET_STRING_set(octets.get(), extension_value.data(), static_cast<int>(extension_value.size())) != 1) {
      throw std::runtime_error{"failed to create test libp2p extension value"};
   }
   auto extension = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>{
       X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 1, octets.get())};
   if (!extension || X509_add_ext(certificate.get(), extension.get(), -1) != 1 ||
       X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0) {
      throw std::runtime_error{"failed to sign test certificate"};
   }

   auto certificate_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   if (!certificate_bio || PEM_write_bio_X509(certificate_bio.get(), certificate.get()) != 1) {
      throw std::runtime_error{"failed to write test certificate PEM"};
   }

   auto out = test_certificate_identity{
       .certificate_pem = bio_to_string(certificate_bio.get()),
       .private_key_pem = private_key_pem,
   };
   out.peer = make_peer_id(identity_key);
   return out;
}

std::string_view test_certificate() {
   return "-----BEGIN CERTIFICATE-----\n"
          "MIICpDCCAYwCCQCJjaEDxrQqBzANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
          "MjcuMC4wLjEwHhcNMjYwNDI5MDgwMTMzWhcNMjYwNDMwMDgwMTMzWjAUMRIwEAYD\n"
          "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDy\n"
          "sbPH/R4QUz725sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TX\n"
          "gl9tHkNpKmI92s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+\n"
          "x7MRWXfKYd/ArGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOS\n"
          "lI/lDqIjZxo7jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuv\n"
          "M+mTj6eO4UQ42w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXj\n"
          "nPOZzBinLRTDnE59HbDZAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHSOUQTEDgjC\n"
          "uwza9ayfThJTs43j+TziWHLlowqCiHt/ipRNFEW7L0ibTnbMdQBFGfaLkTAhc5Rd\n"
          "6O6x+9o76pgEYxEg0rDkgNXmprNmS+nL7Are+iiF6R+X8dts3MQgtONPApAXE96P\n"
          "/n5K4GDQTd3WCI37hkmJA6rmwziFDTlwqtKWts39g8PqAbXac27rVR/iD0gWdOws\n"
          "qiaoGj/0WW9qcgjYGdCc0/CbbnyiWbi48VVf0yyfm7wgcz90byaKIQchHdb/qjyU\n"
          "wy7nfU5TJ5MKQ5yeqPTWmPYZZp9TKa5VD6wZD/IH7jH3GdJ/fSyroVLZktVnmxJa\n"
          "dmG/9wwivwQ=\n"
          "-----END CERTIFICATE-----\n";
}

std::string_view test_private_key() {
   return "-----BEGIN PRIVATE KEY-----\n"
          "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDysbPH/R4QUz72\n"
          "5sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TXgl9tHkNpKmI9\n"
          "2s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+x7MRWXfKYd/A\n"
          "rGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOSlI/lDqIjZxo7\n"
          "jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuvM+mTj6eO4UQ4\n"
          "2w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXjnPOZzBinLRTD\n"
          "nE59HbDZAgMBAAECggEBAIWVjHhy+V5RA+JRCh/12ayirNLG2BF30OP9pf7iL4IT\n"
          "/dMPbKvkmDGLw+1bW8tgKXj5+N6N/trfCm4zhqI3OF7ihooH9qYM88/F/OvMjFiU\n"
          "BhMVVhJW1LxtPPjKUcFN58M8VnMhRM9v6gIaoSOJZvpU1abVtgBDocyJUxAB6gYp\n"
          "i7MzoRwHGsL5mW/luE5H92/S8NNwLWBDA7DIGfrTZ6POf92h5I5W3CuTcqR5FICz\n"
          "3pfU3i443yZmsmkc9duH2gZ9cb9j4pRtNLbbsGmRVrBlgnkVFk8JWbikc8MpLeKO\n"
          "VKP7A2NvxJIrc7oFYrf4hbw8P70YL7S9B3W3yBPPzJECgYEA+Y3nG8CtvVTE/Keo\n"
          "qb5Rljlnj9DEffrylLyYUYfSSNR4Olc2WCPBiz0rPCDdO0VGeXAwqLf2VP7IEyAx\n"
          "kvrnqhzHWMhiLv+k4tIVyKCwpuofN0JsoUCi7CwRf+H2Pg+t6ewLV116THKsd41H\n"
          "IRElWyEvZsmbbhlLrsxUtfFZWnUCgYEA+PZwXUn+cb8kRmfG959gMawTtcfvnBUX\n"
          "sIn7LQl/ZWUIiLMWCaS3FbqkiGjaEYo6om1invYNJNA9zp/ECauSDp58NICCL0ie\n"
          "L7z26sEa6Ocg2VdR4ezpN3cM6dyAKfTFGb9V6qjyqNIPCE4eey6ZJ+CU/mpEfSDu\n"
          "+RGMzfdDCFUCgYEA5FRUn0zk6jU0YyMXq+9pgLSXL7vI/Kdt6m7AQuCto1tbga2o\n"
          "GG7mt/pIo6RCJufUemoO62AeL1hKQU2UbjHJYxkfv/jf9LaM68dijQWRe7b8xres\n"
          "4sFcEBCmFkbt4YzBCCWjntT1gBrv+Ba4fOXOMxoi374Yy1yzpYRpAWuI4L0CgYAn\n"
          "u1SlXrivuHx2i/tR62pzou2mVhkkRK16LBsczeY57UzWXBZJRbM+UYIOjwU2RWQk\n"
          "JebWTZg9ZspmXlLv5CS0FpDl5BhiqWktXy/cuSKtRq2UYf4cWy3A/0vdSqZdi8Wk\n"
          "3Uc94uaPEK77eVQd/orMtWexzo3NlmLs9uMMv8g/3QKBgQCbik0UoJkkqNRMmWG8\n"
          "dKQzj58eRI8fmKdJlWNfj2QMspd2vXMbsWYgAbFbU1QcVs1n8PxNydM+cfy77w8q\n"
          "NWMlYP7rUFQ3ekYWqrRlshZdJ/h24PALd1nPCvhc4C9dvn+zW3BLVez1lBuFO8n8\n"
          "0YkgmTgW7Ieibqnf4DqYp//nkw==\n"
          "-----END PRIVATE KEY-----\n";
}

peer_id legacy_cert_hash_peer_id(std::string_view certificate_pem) {
   const auto certificate = fcl::crypto::x509::certificate::from_pem(certificate_pem);
   const auto der = certificate.der();
   return peer_id::from_bytes(fcl::multiformats::multihash::sha2_256(der).encode());
}

peer_id peer(std::uint8_t value) {
   const auto payload = fcl::multiformats::bytes{value};
   return peer_id::from_bytes(fcl::multiformats::multihash::identity(payload).encode());
}

std::uint8_t hex_value(char value) {
   if (value >= '0' && value <= '9') {
      return static_cast<std::uint8_t>(value - '0');
   }
   if (value >= 'a' && value <= 'f') {
      return static_cast<std::uint8_t>(10 + value - 'a');
   }
   if (value >= 'A' && value <= 'F') {
      return static_cast<std::uint8_t>(10 + value - 'A');
   }
   throw std::runtime_error{"bad hex"};
}

std::vector<std::uint8_t> bytes_from_hex(std::string_view hex) {
   if ((hex.size() % 2) != 0) {
      throw std::runtime_error{"odd hex"};
   }
   auto out = std::vector<std::uint8_t>{};
   out.reserve(hex.size() / 2);
   for (std::size_t i = 0; i < hex.size(); i += 2) {
      out.push_back(static_cast<std::uint8_t>((hex_value(hex[i]) << 4U) | hex_value(hex[i + 1])));
   }
   return out;
}

void append_der_length(std::vector<std::uint8_t>& out, std::size_t value) {
   if (value < 128) {
      out.push_back(static_cast<std::uint8_t>(value));
      return;
   }
   auto bytes = std::vector<std::uint8_t>{};
   while (value != 0) {
      bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
      value >>= 8U;
   }
   out.push_back(static_cast<std::uint8_t>(0x80U | bytes.size()));
   for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
      out.push_back(*it);
   }
}

void append_der_octet_string(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> value) {
   out.push_back(0x04);
   append_der_length(out, value.size());
   out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> signed_key_der(std::span<const std::uint8_t> public_key,
                                         std::span<const std::uint8_t> signature) {
   auto content = std::vector<std::uint8_t>{};
   append_der_octet_string(content, public_key);
   append_der_octet_string(content, signature);
   auto out = std::vector<std::uint8_t>{0x30};
   append_der_length(out, content.size());
   out.insert(out.end(), content.begin(), content.end());
   return out;
}

std::vector<std::uint8_t> signed_key_der_with_overflowing_octet_length() {
   auto out = std::vector<std::uint8_t>{0x30, 0x0a, 0x04, 0x88};
   out.insert(out.end(), 8, 0xff);
   return out;
}

std::vector<std::uint8_t> certificate_public_key_der(X509* certificate) {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{X509_get_pubkey(certificate)};
   if (!key) {
      throw std::runtime_error{"failed to get certificate public key"};
   }
   const auto length = i2d_PUBKEY(key.get(), nullptr);
   if (length <= 0) {
      throw std::runtime_error{"failed to size certificate public key DER"};
   }
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key.get(), &cursor) != length) {
      throw std::runtime_error{"failed to write certificate public key DER"};
   }
   return out;
}

template <typename ExtensionFactory>
std::vector<std::uint8_t> make_certificate_der_with_libp2p_extension_from_factory(ExtensionFactory make_extension) {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")};
   if (!key) {
      throw std::runtime_error{"failed to generate certificate key"};
   }
   auto certificate = std::unique_ptr<X509, x509_deleter>{X509_new()};
   if (!certificate || X509_set_version(certificate.get(), 2) != 1 ||
       ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 77) != 1 ||
       X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) == nullptr ||
       X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 24 * 60 * 60) == nullptr ||
       X509_set_pubkey(certificate.get(), key.get()) != 1) {
      throw std::runtime_error{"failed to configure certificate"};
   }
   auto* name = X509_get_subject_name(certificate.get());
   const auto common_name = std::string_view{"fcl-libp2p-identity-test"};
   if (name == nullptr ||
       X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name.data()),
                                  static_cast<int>(common_name.size()), -1, 0) != 1 ||
       X509_set_issuer_name(certificate.get(), name) != 1) {
      throw std::runtime_error{"failed to configure certificate subject"};
   }
   const auto public_key_der = certificate_public_key_der(certificate.get());
   const auto extension_value = make_extension(std::span<const std::uint8_t>{public_key_der});
   auto object = std::unique_ptr<ASN1_OBJECT, asn1_object_deleter>{OBJ_txt2obj("1.3.6.1.4.1.53594.1.1", 1)};
   auto octets = std::unique_ptr<ASN1_OCTET_STRING, asn1_octet_string_deleter>{ASN1_OCTET_STRING_new()};
   if (!object || !octets ||
       ASN1_OCTET_STRING_set(octets.get(), extension_value.data(), static_cast<int>(extension_value.size())) != 1) {
      throw std::runtime_error{"failed to create certificate extension value"};
   }
   auto extension = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>{
       X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 1, octets.get())};
   if (!extension || X509_add_ext(certificate.get(), extension.get(), -1) != 1 ||
       X509_sign(certificate.get(), key.get(), nullptr) <= 0) {
      throw std::runtime_error{"failed to sign certificate"};
   }
   const auto length = i2d_X509(certificate.get(), nullptr);
   if (length <= 0) {
      throw std::runtime_error{"failed to size certificate DER"};
   }
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_X509(certificate.get(), &cursor) != length) {
      throw std::runtime_error{"failed to write certificate DER"};
   }
   return out;
}

std::vector<std::uint8_t> make_certificate_der_with_libp2p_extension(std::span<const std::uint8_t> extension_value) {
   return make_certificate_der_with_libp2p_extension_from_factory([&](std::span<const std::uint8_t>) {
      return std::vector<std::uint8_t>{extension_value.begin(), extension_value.end()};
   });
}

std::vector<std::uint8_t> tls_identity_message(std::span<const std::uint8_t> certificate_public_key) {
   auto out = std::vector<std::uint8_t>{};
   constexpr auto prefix = std::string_view{"libp2p-tls-handshake:"};
   out.insert(out.end(), prefix.begin(), prefix.end());
   out.insert(out.end(), certificate_public_key.begin(), certificate_public_key.end());
   return out;
}

std::vector<std::uint8_t> sign_test_identity(const test_identity& identity, std::span<const std::uint8_t> message) {
   return identity.private_key.visit([&](const auto& key) -> std::vector<std::uint8_t> {
      using key_type = std::decay_t<decltype(key)>;
      if constexpr (std::is_same_v<key_type, fcl::crypto::secp256k1::private_key_shim>) {
         return fcl::crypto::secp256k1::sign_der(key, message);
      } else if constexpr (std::is_same_v<key_type, fcl::crypto::p256::private_key_shim>) {
         return fcl::crypto::p256::sign_der(key, message);
      } else {
         return bytes_from_range(key.sign(message).serialize());
      }
   });
}

std::vector<std::uint8_t> signed_tls_extension(const test_identity& identity,
                                               std::span<const std::uint8_t> certificate_public_key) {
   const auto message = tls_identity_message(certificate_public_key);
   const auto signature = sign_test_identity(identity, message);
   return signed_key_der(encode_public_key(identity.key), signature);
}

node::options options_for(peer_id id, capability_set capabilities = capability_set{
                                          .bits = capabilities::direct_quic | capabilities::peer_exchange}) {
   return node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = std::move(id),
       .capabilities = capabilities,
       .allow_insecure_test_mode = true,
   };
}

node::options options_for(const test_certificate_identity& identity,
                          capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                               capabilities::peer_exchange}) {
   return node::options{
       .certificate_pem = identity.certificate_pem,
       .private_key_pem = identity.private_key_pem,
       .capabilities = capabilities,
       .allow_insecure_test_mode = true,
   };
}

node::options options_for(const test_identity& identity,
                          capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                               capabilities::peer_exchange}) {
   auto out = options_for(identity.peer, capabilities);
   out.private_key_pem = identity.private_key_pem;
   out.public_key = encode_public_key(identity.key);
   return out;
}

public_key test_rsa_public_key() {
   return public_key{
       .type = public_key::type::rsa,
       .data =
           fcl::crypto::der::write_public_key(fcl::crypto::pem::read_private_key(test_private_key()).get_public_key()),
   };
}

node::options pubsub_options_for(capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                                      capabilities::pubsub}) {
   const auto key = test_rsa_public_key();
   auto out = options_for(make_peer_id(key), capabilities);
   out.public_key = encode_public_key(key);
   return out;
}

node::options pubsub_options_for(const test_certificate_identity& identity,
                                 capability_set capabilities = capability_set{.bits = capabilities::direct_quic |
                                                                                      capabilities::pubsub}) {
   return options_for(identity, capabilities);
}

std::filesystem::path temp_store_path(std::string_view name) {
   auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
   auto path = std::filesystem::temp_directory_path() / ("fcl-p2p-" + std::string{name} + "-" + std::to_string(stamp));
   std::filesystem::remove_all(path);
   return path;
}

class temp_store_dir {
 public:
   explicit temp_store_dir(std::string_view name) : path_(temp_store_path(name)) {}

   ~temp_store_dir() {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
   }

   [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
   }

 private:
   std::filesystem::path path_;
};

void register_echo(node& value) {
   value.register_protocol_handler(builtins::echo,
                                   [](node::incoming_protocol_stream incoming) mutable -> boost::asio::awaitable<void> {
                                      auto payload = co_await incoming.stream.async_read_frame();
                                      co_await incoming.stream.async_write_frame(payload);
                                   });
}

void register_echo(node& value, protocol_id protocol) {
   value.register_protocol_handler(std::move(protocol),
                                   [](node::incoming_protocol_stream incoming) mutable -> boost::asio::awaitable<void> {
                                      auto payload = co_await incoming.stream.async_read_frame();
                                      co_await incoming.stream.async_write_frame(payload);
                                   });
}

[[nodiscard]] endpoint make_quic_endpoint(std::uint16_t port, std::string host = "127.0.0.1") {
   return endpoint{.transport = {.host_type = endpoint::host_kind::ip4,
                                 .protocol = endpoint::protocol_kind::quic_v1,
                                 .host = std::move(host),
                                 .port = port}};
}

[[nodiscard]] endpoint make_tcp_endpoint(std::uint16_t port, std::string host = "127.0.0.1") {
   return endpoint{.transport = {.host_type = endpoint::host_kind::ip4,
                                 .protocol = endpoint::protocol_kind::tcp,
                                 .host = std::move(host),
                                 .port = port}};
}

[[nodiscard]] endpoint make_dns_tcp_endpoint(std::uint16_t port, std::string host) {
   return endpoint{.transport = {.host_type = endpoint::host_kind::dns,
                                 .protocol = endpoint::protocol_kind::tcp,
                                 .host = std::move(host),
                                 .port = port}};
}

endpoint listen(node& value, fcl::asio::runtime& runtime) {
   fcl::asio::blocking::run(runtime, value.async_listen(make_quic_endpoint(0)));
   auto endpoint = value.local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   return *endpoint;
}

endpoint listen_tcp(node& value, fcl::asio::runtime& runtime) {
   fcl::asio::blocking::run(runtime, value.async_listen(make_tcp_endpoint(0)));
   auto endpoint = value.local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   return *endpoint;
}

[[nodiscard]] bool contains_protocol(const std::vector<endpoint>& endpoints, endpoint::protocol_kind protocol) {
   return std::ranges::any_of(endpoints,
                              [protocol](const endpoint& value) { return value.transport.protocol == protocol; });
}

[[nodiscard]] endpoint require_endpoint_for(const std::vector<endpoint>& endpoints, endpoint::protocol_kind protocol) {
   auto found = std::ranges::find_if(
       endpoints, [protocol](const endpoint& value) { return value.transport.protocol == protocol; });
   BOOST_REQUIRE(found != endpoints.end());
   return *found;
}

endpoint start_stalling_tcp_peer(fcl::asio::runtime& runtime,
                                 std::chrono::milliseconds hold = std::chrono::seconds{2}) {
   namespace asio = boost::asio;
   using asio_tcp = asio::ip::tcp;
   auto acceptor = std::make_shared<asio_tcp::acceptor>(runtime.context(), asio_tcp::endpoint{asio_tcp::v4(), 0});
   auto socket = std::make_shared<asio_tcp::socket>(runtime.context());
   const auto port = acceptor->local_endpoint().port();

   asio::co_spawn(
       runtime.context(),
       [acceptor, socket, hold]() -> asio::awaitable<void> {
          auto error = boost::system::error_code{};
          co_await acceptor->async_accept(*socket, asio::redirect_error(asio::use_awaitable, error));
          if (!error) {
             auto timer = asio::steady_timer{co_await asio::this_coro::executor};
             timer.expires_after(hold);
             co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
          }
          auto ignored = boost::system::error_code{};
          socket->close(ignored);
          acceptor->close(ignored);
       },
       asio::detached);

   return make_tcp_endpoint(port);
}

void wait_for_server(std::future<void>& future, std::chrono::milliseconds timeout, std::string_view label) {
   if (future.wait_for(timeout) != std::future_status::ready) {
      throw std::runtime_error{std::string{label} + " did not finish"};
   }
   future.get();
}

void wait_on_runtime(fcl::asio::runtime& runtime, std::chrono::milliseconds delay, std::string_view label) {
   auto future = boost::asio::co_spawn(
       runtime.context(),
       [delay]() -> boost::asio::awaitable<void> {
          auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
          timer.expires_after(delay);
          boost::system::error_code ec;
          co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
       },
       boost::asio::use_future);
   wait_for_server(future, delay + std::chrono::milliseconds{1'000}, label);
}

std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload) {
   auto out = fcl::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes, std::size_t max_payload_size) {
   const auto decoded = fcl::multiformats::varint_decode(bytes);
   BOOST_REQUIRE(decoded.value <= max_payload_size);
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   BOOST_REQUIRE_EQUAL(total, bytes.size());
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

boost::asio::awaitable<std::vector<std::uint8_t>>
read_length_delimited(stream& value, std::size_t max_payload_size = 4 * 1024 * 1024) {
   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      try {
         const auto decoded = fcl::multiformats::varint_decode(buffer);
         BOOST_REQUIRE(decoded.value <= max_payload_size);
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto frame = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            co_return unwrap_length_delimited(frame, max_payload_size);
         }
      } catch (const fcl::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            throw;
         }
      }
      auto chunk = co_await value.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

class queued_transport_stream final : public fcl::transport::detail::stream_concept {
 public:
   explicit queued_transport_stream(std::int64_t stream_id) : stream_id_{stream_id} {}

   [[nodiscard]] bool valid() const noexcept override {
      return true;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return stream_id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      writes.push_back({bytes.begin(), bytes.end()});
      co_return;
   }

   boost::asio::awaitable<void> async_write_chunk(fcl::transport::chunk bytes) override {
      ++chunk_writes;
      writes.push_back(bytes.to_vector());
      co_return;
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      BOOST_REQUIRE(!reads.empty());
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return out;
   }

   boost::asio::awaitable<fcl::transport::chunk> async_read_chunk() override {
      ++chunk_reads;
      BOOST_REQUIRE(!reads.empty());
      auto out = std::move(reads.front());
      reads.pop_front();
      co_return fcl::transport::chunk{std::move(out)};
   }

   boost::asio::awaitable<void> async_close() override {
      closed = true;
      co_return;
   }

   void cancel() override {
      closed = true;
   }

   std::deque<std::vector<std::uint8_t>> reads;
   std::vector<std::vector<std::uint8_t>> writes;
   std::size_t chunk_reads = 0;
   std::size_t chunk_writes = 0;
   bool closed = false;

 private:
   std::int64_t stream_id_ = 0;
};

class counting_peer_store_backend final : public peer_store::backend {
 public:
   void upsert(peer_store::record value) override {
      ++upsert_count;
      records[value.peer] = std::move(value);
   }

   void learn_endpoint(peer_id value, fcl::p2p::endpoint endpoint, capability_set capabilities) override {
      ++learn_endpoint_count;
      auto& record = records[value];
      record.peer = std::move(value);
      record.capabilities.bits |= capabilities.bits;
      record.endpoints.push_back(peer_store::endpoint_record{.endpoint = std::move(endpoint)});
   }

   void mark_reachability(peer_id value, reachability::state state,
                          std::optional<fcl::p2p::endpoint> observed) override {
      auto& record = records[value];
      record.peer = std::move(value);
      record.reachability = state;
      record.observed_endpoint = std::move(observed);
   }

   void mark_success(const peer_id& value, path::kind, std::chrono::milliseconds latency) override {
      auto& record = records[value];
      record.peer = value;
      ++record.successes;
      record.last_latency = latency;
   }

   void mark_failure(const peer_id& value) override {
      auto& record = records[value];
      record.peer = value;
      ++record.failures;
   }

   void mark_endpoint_success(const peer_id& value, const fcl::p2p::endpoint& endpoint, path::kind kind,
                              std::chrono::milliseconds latency) override {
      auto& record = records[value];
      record.peer = value;
      record.endpoints.push_back(
          peer_store::endpoint_record{.endpoint = endpoint, .kind = kind, .last_latency = latency});
   }

   void mark_endpoint_failure(const peer_id& value, const fcl::p2p::endpoint& endpoint, path::kind kind,
                              std::chrono::system_clock::time_point backoff_until) override {
      auto& record = records[value];
      record.peer = value;
      record.endpoints.push_back(
          peer_store::endpoint_record{.endpoint = endpoint, .kind = kind, .backoff_until = backoff_until});
   }

   void upsert_routing_peer(dht::peer value, discovery::source source,
                            std::chrono::system_clock::time_point expires_at) override {
      auto& record = records[value.id];
      record.peer = value.id;
      record.discovered_by = source;
      record.discovery_expires_at = expires_at;
      for (const auto& endpoint : value.endpoints) {
         record.endpoints.push_back(peer_store::endpoint_record{.endpoint = endpoint});
      }
   }

   void upsert_provider(peer_store::provider_record value) override {
      providers[value.key.bytes].push_back(std::move(value));
   }

   void upsert_rendezvous(rendezvous::registration value) override {
      value.sequence = ++rendezvous_sequence;
      rendezvous_records.push_back(std::move(value));
   }

   void remove_rendezvous(peer_id value, std::string namespace_name) override {
      std::erase_if(rendezvous_records, [&](const auto& record) {
         return record.peer == value && record.namespace_name == namespace_name;
      });
   }

   [[nodiscard]] std::optional<peer_store::record> find(const peer_id& value) const override {
      const auto it = records.find(value);
      if (it == records.end()) {
         return std::nullopt;
      }
      return it->second;
   }

   [[nodiscard]] std::vector<peer_store::record> snapshot() const override {
      auto out = std::vector<peer_store::record>{};
      out.reserve(records.size());
      for (const auto& [_, record] : records) {
         out.push_back(record);
      }
      return out;
   }

   [[nodiscard]] std::vector<dht::peer> closest_routing_peers(const dht::key&, std::size_t limit) const override {
      auto out = std::vector<dht::peer>{};
      for (const auto& [peer, record] : records) {
         auto endpoints = std::vector<endpoint>{};
         for (const auto& item : record.endpoints) {
            auto discovered = item.endpoint;
            discovered.peer = peer;
            endpoints.push_back(std::move(discovered));
         }
         out.push_back(dht::peer{.id = peer, .endpoints = std::move(endpoints)});
         if (out.size() >= limit) {
            break;
         }
      }
      return out;
   }

   [[nodiscard]] std::vector<peer_store::provider_record> find_providers(const dht::key& key) const override {
      const auto it = providers.find(key.bytes);
      if (it == providers.end()) {
         return {};
      }
      return it->second;
   }

   [[nodiscard]] std::vector<rendezvous::registration> discover_rendezvous(std::string_view namespace_name,
                                                                           std::uint64_t after_sequence,
                                                                           std::size_t limit) const override {
      auto out = std::vector<rendezvous::registration>{};
      for (const auto& record : rendezvous_records) {
         if (!namespace_name.empty() && record.namespace_name != namespace_name) {
            continue;
         }
         if (record.sequence <= after_sequence) {
            continue;
         }
         out.push_back(record);
         if (out.size() >= limit) {
            break;
         }
      }
      return out;
   }

   std::uint32_t upsert_count = 0;
   std::uint32_t learn_endpoint_count = 0;
   std::map<peer_id, peer_store::record> records;
   std::map<std::vector<std::uint8_t>, std::vector<peer_store::provider_record>> providers;
   std::vector<rendezvous::registration> rendezvous_records;
   std::uint64_t rendezvous_sequence = 0;
};

} // namespace

BOOST_AUTO_TEST_CASE(p2p_identity_uses_libp2p_multihash_shape) {
   const auto identity = make_test_certificate_identity("p2p-identity-shape");
   const auto id = make_peer_id_from_certificate_pem(identity.certificate_pem);

   BOOST_TEST(id.to_string() == identity.peer.to_string());
   BOOST_TEST(valid_peer_id(id));
   auto decoded = fcl::multiformats::multihash::decode(id.to_bytes());
   BOOST_TEST(decoded.code == fcl::multiformats::code_value(fcl::multiformats::multicodec_code::sha2_256));
   BOOST_TEST(decoded.digest.size() == 32U);
}

BOOST_AUTO_TEST_CASE(p2p_certificate_without_libp2p_extension_is_rejected) {
   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_pem(test_certificate()), exceptions::invalid_identity);
   const auto certificate = fcl::crypto::x509::certificate::from_pem(test_certificate());
   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_der(certificate.der()), exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_public_key_encoding_matches_libp2p_vectors) {
   const auto ed25519_public_key = bytes_from_hex("1ed1e8fae2c4a144b8be8fd4b47bf3d3b34b871c3cacf6010f0e42d474fce27e");
   const auto ed25519_encoded = encode_public_key({.type = public_key::type::ed25519, .data = ed25519_public_key});
   BOOST_CHECK_EQUAL(fcl::multiformats::multihash::identity(ed25519_encoded).digest_hex(),
                     "080112201ed1e8fae2c4a144b8be8fd4b47bf3d3b34b871c3cacf6010f0e42d474fce27e");

   auto ed25519_peer = make_peer_id({.type = public_key::type::ed25519, .data = ed25519_public_key});
   auto ed25519_hash = fcl::multiformats::multihash::decode(ed25519_peer.to_bytes());
   BOOST_TEST(ed25519_hash.code == fcl::multiformats::code_value(fcl::multiformats::multicodec_code::identity));

   const auto ecdsa_public_key =
       bytes_from_hex("3059301306072a8648ce3d020106082a8648ce3d03010703420004de3d300fa36ae0e8f5d530899d83abab44ab"
                      "f3161f162a4bc901d8e6ecda020e8b6d5f8da30525e71d6851510c098e5c47c646a597fb4dcec034e9f77c409e62");
   auto ecdsa_peer = make_peer_id({.type = public_key::type::ecdsa, .data = ecdsa_public_key});
   auto ecdsa_hash = fcl::multiformats::multihash::decode(ecdsa_peer.to_bytes());
   BOOST_TEST(ecdsa_hash.code == fcl::multiformats::code_value(fcl::multiformats::multicodec_code::sha2_256));
}

BOOST_AUTO_TEST_CASE(p2p_certificate_extension_rejects_unverified_non_ed25519_identity) {
   const auto identity = make_secp256k1_identity();
   auto bogus_signature = std::vector<std::uint8_t>(72, 0x42);
   const auto extension = signed_key_der(encode_public_key(identity.key), bogus_signature);
   const auto certificate = make_certificate_der_with_libp2p_extension(extension);

   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_der(certificate), exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_certificate_extension_rejects_overflowing_identity_octet_length) {
   const auto extension = signed_key_der_with_overflowing_octet_length();
   const auto certificate = make_certificate_der_with_libp2p_extension(extension);

   BOOST_CHECK_THROW((void)make_peer_id_from_certificate_der(certificate), exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_certificate_extension_verifies_supported_identities) {
   const auto identities = std::vector<test_identity>{make_test_identity(), make_rsa_identity(),
                                                      make_secp256k1_identity(), make_p256_identity()};
   for (const auto& identity : identities) {
      const auto certificate = make_certificate_der_with_libp2p_extension_from_factory(
          [&](std::span<const std::uint8_t> certificate_public_key) {
             return signed_tls_extension(identity, certificate_public_key);
          });

      BOOST_TEST(make_peer_id_from_certificate_der(certificate).to_string() == identity.peer.to_string());
   }
}

BOOST_AUTO_TEST_CASE(p2p_identity_signatures_support_supported_key_types) {
   const auto identities = std::vector<test_identity>{make_test_identity(), make_rsa_identity(),
                                                      make_secp256k1_identity(), make_p256_identity()};
   for (const auto& identity : identities) {
      auto message = pubsub::message{
          .from = identity.peer,
          .data = std::vector<std::uint8_t>{'m', 'u', 'l', 't', 'i', '-', 'k', 'e', 'y'},
          .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 9},
          .subject = pubsub::topic{.value = "fcl.identity"},
          .key = encode_public_key(identity.key),
      };
      pubsub::codec::sign_message(message, identity.private_key);
      BOOST_TEST(pubsub::codec::verify_message(message));

      auto tampered = message;
      tampered.signature.back() ^= 0x01U;
      BOOST_TEST(!pubsub::codec::verify_message(tampered));

      const auto payload_type = fcl::multiformats::varint_encode(0x0302);
      const auto payload = std::vector<std::uint8_t>{7, 8, 9};
      const auto envelope =
          signed_envelope::seal(identity.key, identity.private_key, "libp2p-relay-rsvp", payload_type, payload);
      BOOST_CHECK_NO_THROW(envelope.verify("libp2p-relay-rsvp", identity.peer));

      auto tampered_envelope = envelope;
      tampered_envelope.payload.back() ^= 0x01U;
      BOOST_CHECK_THROW(tampered_envelope.verify("libp2p-relay-rsvp", identity.peer), exceptions::invalid_identity);

      auto malformed_envelope = envelope;
      malformed_envelope.signature.pop_back();
      BOOST_CHECK_THROW(malformed_envelope.verify("libp2p-relay-rsvp", identity.peer), exceptions::invalid_identity);
   }
}

BOOST_AUTO_TEST_CASE(p2p_peer_id_legacy_and_cid_strings_roundtrip) {
   auto id =
       make_peer_id({.type = public_key::type::secp256k1,
                     .data = bytes_from_hex("037777e994e452c21604f91de093ce415f5432f701dd8cd1a7a6fea0e630bfca99")});

   auto legacy = id.to_string();
   BOOST_TEST(peer_id::from_string(legacy).to_string() == id.to_string());

   auto cid = id.to_cid_string();
   BOOST_TEST(cid.front() == 'b');
   BOOST_TEST(peer_id::from_string(cid).to_string() == id.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_endpoint_parses_libp2p_quic_address_format) {
   static_assert(std::is_same_v<decltype(endpoint{}.transport.host_type), endpoint::host_kind>);

   const auto id = peer(42);
   auto parsed = parse_endpoint("/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string());

   BOOST_TEST(static_cast<int>(parsed.transport.host_type) == static_cast<int>(endpoint::host_kind::ip4));

   BOOST_TEST(parsed.transport.host == "127.0.0.1");
   BOOST_TEST(parsed.transport.port == 4001);
   BOOST_REQUIRE(parsed.peer.has_value());
   BOOST_TEST(parsed.peer->to_string() == id.to_string());
   BOOST_TEST(parsed.to_string() == "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string());
   BOOST_TEST(parsed.is_direct_quic());
}

BOOST_AUTO_TEST_CASE(p2p_endpoint_uses_multiaddr_for_tcp_wss_and_relay_views) {
   const auto id = peer(43);

   auto tcp = parse_endpoint("/dns4/example.com/tcp/4001/p2p/" + id.to_string());
   BOOST_TEST(static_cast<int>(tcp.transport.host_type) == static_cast<int>(endpoint::host_kind::dns4));
   BOOST_TEST(static_cast<int>(tcp.transport.protocol) == static_cast<int>(endpoint::protocol_kind::tcp));
   BOOST_TEST(tcp.transport.host == "example.com");
   BOOST_TEST(tcp.transport.port == 4001);
   BOOST_TEST(tcp.is_direct_tcp());
   BOOST_TEST(tcp.to_string() == "/dns4/example.com/tcp/4001/p2p/" + id.to_string());

   auto wss = parse_endpoint("/dns4/example.com/tcp/443/wss/p2p/" + id.to_string());
   BOOST_TEST(!wss.is_direct_tcp());
   BOOST_TEST(!wss.is_direct_quic());
   BOOST_TEST(wss.to_string() == "/dns4/example.com/tcp/443/wss/p2p/" + id.to_string());

   auto relayed = parse_endpoint("/ip4/127.0.0.1/tcp/9090/p2p-circuit/p2p/" + id.to_string());
   BOOST_TEST(!relayed.peer.has_value());
   BOOST_REQUIRE(relayed.relayed.has_value());
   BOOST_TEST(relayed.relayed->target.to_string() == id.to_string());
   BOOST_TEST(relayed.to_string() == "/ip4/127.0.0.1/tcp/9090/p2p-circuit/p2p/" + id.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_websocket_multiaddr_is_parseable_but_not_dialable) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(44))};
   const auto endpoints = std::vector<endpoint>{
       parse_endpoint("/dns4/example.com/tcp/80/ws/p2p/" + peer(46).to_string()),
       parse_endpoint("/dns4/example.com/tcp/443/wss/p2p/" + peer(47).to_string()),
   };

   for (const auto& endpoint : endpoints) {
      try {
         fcl::asio::blocking::run(runtime, value.async_listen(endpoint));
         BOOST_FAIL("expected unsupported listen endpoint");
      } catch (const fcl::exceptions::base& error) {
         BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
         BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                    static_cast<int>(exceptions::code::unsupported_protocol));
      }

      try {
         fcl::asio::blocking::run(runtime, value.async_connect(endpoint));
         BOOST_FAIL("expected unsupported connect endpoint");
      } catch (const fcl::exceptions::base& error) {
         BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
         BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                    static_cast<int>(exceptions::code::unsupported_protocol));
      }
   }
}

BOOST_AUTO_TEST_CASE(p2p_node_listens_on_quic_and_tcp_and_identify_advertises_both) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(210))};
   auto client = node{runtime, options_for(peer(211))};

   fcl::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   fcl::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));

   const auto local = server.local_endpoints();
   BOOST_REQUIRE_EQUAL(local.size(), 2U);
   BOOST_TEST(contains_protocol(local, endpoint::protocol_kind::quic_v1));
   BOOST_TEST(contains_protocol(local, endpoint::protocol_kind::tcp));
   for (const auto& item : local) {
      BOOST_REQUIRE(item.peer.has_value());
      BOOST_TEST(item.peer->value == server.local_peer().value);
   }
   BOOST_REQUIRE(server.local_endpoint().has_value());

   const auto quic = require_endpoint_for(local, endpoint::protocol_kind::quic_v1);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(quic, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto payload = fcl::asio::blocking::run(runtime, read_length_delimited(stream));
   const auto doc = identify::decode(payload);
   BOOST_TEST(contains_protocol(doc.listen_endpoints, endpoint::protocol_kind::quic_v1));
   BOOST_TEST(contains_protocol(doc.listen_endpoints, endpoint::protocol_kind::tcp));
   for (const auto& item : doc.listen_endpoints) {
      BOOST_REQUIRE(item.peer.has_value());
      BOOST_TEST(item.peer->value == server.local_peer().value);
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_duplicate_direct_listen_rejects_typed) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(212))};

   fcl::asio::blocking::run(runtime, value.async_listen(make_tcp_endpoint(0)));
   const auto local = require_endpoint_for(value.local_endpoints(), endpoint::protocol_kind::tcp);

   try {
      fcl::asio::blocking::run(runtime, value.async_listen(local));
      BOOST_FAIL("expected duplicate listen rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(*fcl::p2p::exceptions::code_of(error)) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }

   fcl::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_preserves_multiple_direct_endpoints_without_duplicates) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("peer-exchange-multi-server");
   const auto client_identity = make_test_certificate_identity("peer-exchange-multi-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   fcl::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   fcl::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   const auto advertised = server.local_endpoints();
   BOOST_REQUIRE_EQUAL(advertised.size(), 2U);

   const auto quic = require_endpoint_for(advertised, endpoint::protocol_kind::quic_v1);
   client.peers().learn_endpoint(server.local_peer(), quic,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange});
   fcl::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));
   fcl::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));

   const auto learned = client.peers().find(server.local_peer());
   BOOST_REQUIRE(learned);
   auto learned_endpoints = std::vector<endpoint>{};
   learned_endpoints.reserve(learned->endpoints.size());
   for (const auto& item : learned->endpoints) {
      learned_endpoints.push_back(item.endpoint);
   }
   BOOST_TEST(contains_protocol(learned_endpoints, endpoint::protocol_kind::quic_v1));
   BOOST_TEST(contains_protocol(learned_endpoints, endpoint::protocol_kind::tcp));
   auto seen = std::set<std::string>{};
   for (const auto& item : learned->endpoints) {
      BOOST_REQUIRE(item.endpoint.peer.has_value());
      BOOST_TEST(item.endpoint.peer->to_bytes() == server.local_peer().to_bytes(), boost::test_tools::per_element());
      BOOST_TEST(seen.insert(item.endpoint.to_string()).second);
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_local_endpoints_collapse_canonical_equivalent_advertised_endpoints) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   const auto local = peer(219);
   auto configured = make_tcp_endpoint(4001, "127.0.0.1");
   configured.peer = local;
   auto equivalent = parse_endpoint(configured.to_string());
   equivalent.peer = std::nullopt;
   auto options = options_for(local);
   options.advertised_endpoints = {configured, equivalent};
   auto value = node{runtime, std::move(options)};

   const auto endpoints = value.local_endpoints();
   BOOST_REQUIRE_EQUAL(endpoints.size(), 1U);
   BOOST_REQUIRE(endpoints.front().peer.has_value());
   BOOST_TEST(endpoints.front().peer->to_bytes() == local.to_bytes(), boost::test_tools::per_element());
   BOOST_TEST(endpoints.front().to_string() == configured.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_filters_non_routable_third_party_endpoints) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("peer-exchange-filter-server");
   const auto client_identity = make_test_certificate_identity("peer-exchange-filter-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};
   const auto third = peer(220);

   auto loopback = make_tcp_endpoint(4001, "127.0.0.1");
   auto private_endpoint = make_tcp_endpoint(4002, "192.168.1.10");
   auto link_local = make_tcp_endpoint(4003, "169.254.1.10");
   auto public_endpoint = make_tcp_endpoint(4004, "8.8.4.4");
   auto dns_endpoint = make_dns_tcp_endpoint(4005, "example.com");
   auto localhost_dns = make_dns_tcp_endpoint(4006, "localhost");
   for (auto* endpoint_value :
        {&loopback, &private_endpoint, &link_local, &public_endpoint, &dns_endpoint, &localhost_dns}) {
      endpoint_value->peer = third;
      server.peers().learn_endpoint(third, *endpoint_value, capability_set{});
   }

   fcl::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   const auto quic = require_endpoint_for(server.local_endpoints(), endpoint::protocol_kind::quic_v1);
   client.peers().learn_endpoint(server.local_peer(), quic,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange});
   fcl::asio::blocking::run(runtime, client.async_request_peer_exchange(server.local_peer()));

   const auto learned = client.peers().find(third);
   BOOST_REQUIRE(learned);
   auto seen = std::set<std::string>{};
   for (const auto& item : learned->endpoints) {
      seen.insert(item.endpoint.to_string());
   }
   BOOST_TEST(seen.contains(public_endpoint.to_string()));
   BOOST_TEST(seen.contains(dns_endpoint.to_string()));
   BOOST_TEST(!seen.contains(loopback.to_string()));
   BOOST_TEST(!seen.contains(private_endpoint.to_string()));
   BOOST_TEST(!seen.contains(link_local.to_string()));
   BOOST_TEST(!seen.contains(localhost_dns.to_string()));

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_stop_closes_all_direct_listeners) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(216))};
   fcl::asio::blocking::run(runtime, server.async_listen(make_quic_endpoint(0)));
   fcl::asio::blocking::run(runtime, server.async_listen(make_tcp_endpoint(0)));
   const auto local = server.local_endpoints();
   const auto quic = require_endpoint_for(local, endpoint::protocol_kind::quic_v1);
   const auto tcp = require_endpoint_for(local, endpoint::protocol_kind::tcp);

   fcl::asio::blocking::run(runtime, server.async_stop());

   auto rebound = node{runtime, options_for(peer(217))};
   fcl::asio::blocking::run(runtime, rebound.async_listen(quic));
   auto tcp_rebound = fcl::tcp::listener{runtime.context().get_executor(), tcp.transport};
   BOOST_TEST(tcp_rebound.valid());

   fcl::asio::blocking::run(runtime, tcp_rebound.async_close());
   fcl::asio::blocking::run(runtime, rebound.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_nodes_prefer_tls_yamux_and_echo_frames) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_identity();
   const auto client_identity = make_test_identity();
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};
   register_echo(server);

   const auto server_endpoint = listen_tcp(server, runtime);
   BOOST_TEST(server_endpoint.is_direct_tcp());

   const auto session = fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo));
   const auto payload = std::vector<std::uint8_t>{'t', 'c', 'p'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);
   BOOST_TEST(server.metrics().protocol_streams_accepted >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_rejects_tls_peer_mismatch) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_identity();
   const auto client_identity = make_test_identity();
   auto server_options = options_for(server_identity);
   auto client_options = options_for(client_identity);
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_store_backend = peer_store::make_memory_backend();
   client_options.peer_store_backend = peer_store::make_memory_backend();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto server_endpoint = listen_tcp(server, runtime);
   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = peer(150)}));
      BOOST_FAIL("expected TCP TLS peer mismatch");
   } catch (const fcl::exceptions::base& error) {
      BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_upgrade_honors_attempt_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto client = node{runtime, options_for(peer(201))};
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime);

   auto saw_timeout = false;
   const auto completed = fcl::asio::blocking::run_for(
       runtime,
       [&]() -> boost::asio::awaitable<void> {
          try {
             (void)co_await client.async_connect(stalled_endpoint,
                                                 node::connect_options{.expected_peer = peer(202),
                                                                       .allow_relay = false,
                                                                       .timeout = std::chrono::milliseconds{100}});
             BOOST_FAIL("expected stalled TCP direct connect timeout");
          } catch (const fcl::exceptions::base& error) {
             BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
             BOOST_TEST(static_cast<int>(*fcl::p2p::exceptions::code_of(error)) ==
                        static_cast<int>(exceptions::code::timeout));
             saw_timeout = true;
          }
       }(),
       std::chrono::milliseconds{1'000});

   BOOST_TEST(completed);
   BOOST_TEST(saw_timeout);
   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_tcp_stop_closes_listener_port) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto server = node{runtime, options_for(peer(203))};
   const auto server_endpoint = listen_tcp(server, runtime);

   fcl::asio::blocking::run(runtime, server.async_stop());

   auto rebound = fcl::tcp::listener{runtime.context().get_executor(), server_endpoint.transport};
   BOOST_TEST(rebound.valid());
   fcl::asio::blocking::run(runtime, rebound.async_close());
}

BOOST_AUTO_TEST_CASE(quic_libp2p_profile_sets_required_alpn) {
   auto client = fcl::quic::libp2p::client_profile();
   auto server = fcl::quic::libp2p::server_profile();

   BOOST_TEST(client.alpn == "libp2p");
   BOOST_TEST(server.alpn == "libp2p");
   BOOST_TEST(fcl::quic::libp2p::is_profile_alpn(client.alpn));
}

BOOST_AUTO_TEST_CASE(transport_frame_and_stream_round_trip_payload) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(42);
   auto value = fcl::transport::detail::stream_access::make(backend);
   const auto payload = std::vector<std::uint8_t>{'t', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't'};
   const auto encoded = fcl::transport::encode_frame(payload);
   const auto decoded = fcl::transport::decode_frame(encoded);
   BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(fcl::transport::frame_decode_status::complete));
   BOOST_TEST(decoded.payload == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, value.async_write_frame(payload));
   BOOST_REQUIRE_EQUAL(backend->writes.size(), 1U);
   BOOST_TEST(fcl::transport::decode_frame(backend->writes.front()).payload == payload,
              boost::test_tools::per_element());

   backend->reads.push_back({encoded.begin(), encoded.begin() + 3});
   backend->reads.push_back({encoded.begin() + 3, encoded.end()});
   const auto read = fcl::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_TEST(read == payload, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(p2p_stream_wraps_transport_stream) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(77);
   auto value = fcl::p2p::stream{fcl::transport::detail::stream_access::make(backend)};
   const auto payload = std::vector<std::uint8_t>{'p', '2', 'p'};
   const auto reply = std::vector<std::uint8_t>{'o', 'k'};

   BOOST_TEST(value.valid());
   BOOST_TEST(value.id() == 77);

   fcl::asio::blocking::run(runtime, value.async_write(payload));
   BOOST_REQUIRE_EQUAL(backend->writes.size(), 1U);
   BOOST_TEST(backend->writes.front() == payload, boost::test_tools::per_element());

   backend->reads.push_back(reply);
   const auto read = fcl::asio::blocking::run(runtime, value.async_read());
   BOOST_TEST(read == reply, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, value.async_close());
   BOOST_TEST(backend->closed);
}

BOOST_AUTO_TEST_CASE(p2p_stream_delegates_chunk_read_write_and_preserves_framed_trailing_bytes) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(78);
   auto value = fcl::p2p::stream{fcl::transport::detail::stream_access::make(backend)};
   const auto payload = std::vector<std::uint8_t>{'c', 'h', 'u', 'n', 'k'};
   const auto first = std::vector<std::uint8_t>{'o', 'n', 'e'};
   const auto second = std::vector<std::uint8_t>{'t', 'w', 'o'};

   fcl::asio::blocking::run(runtime, value.async_write(fcl::transport::chunk{payload}));
   BOOST_REQUIRE_EQUAL(backend->chunk_writes, 1U);
   BOOST_REQUIRE_EQUAL(backend->writes.size(), 1U);
   BOOST_TEST(backend->writes.front() == payload, boost::test_tools::per_element());

   backend->reads.push_back(payload);
   auto read = fcl::asio::blocking::run(runtime, value.async_read_chunk());
   BOOST_REQUIRE_EQUAL(backend->chunk_reads, 1U);
   BOOST_TEST(read.to_vector() == payload, boost::test_tools::per_element());

   auto combined = fcl::transport::encode_frame(first);
   auto encoded_second = fcl::transport::encode_frame(second);
   combined.insert(combined.end(), encoded_second.begin(), encoded_second.end());
   backend->reads.push_back(std::move(combined));

   auto first_read = fcl::asio::blocking::run(runtime, value.async_read_frame_chunk());
   BOOST_TEST(first_read.to_vector() == first, boost::test_tools::per_element());
   const auto second_read = fcl::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_TEST(second_read == second, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(backend->chunk_reads, 2U);
}

BOOST_AUTO_TEST_CASE(p2p_stream_with_buffer_preserves_prefetched_framed_chunks) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto backend = std::make_shared<queued_transport_stream>(79);
   auto first = std::vector<std::uint8_t>{'p', 'r', 'e'};
   auto second = std::vector<std::uint8_t>{'b', 'u', 'f'};
   auto buffered = fcl::transport::encode_frame(first);
   auto encoded_second = fcl::transport::encode_frame(second);
   buffered.insert(buffered.end(), encoded_second.begin(), encoded_second.end());

   auto value = fcl::p2p::detail::stream_access::with_buffer(
       fcl::p2p::stream{fcl::transport::detail::stream_access::make(backend)}, std::move(buffered));

   auto first_read = fcl::asio::blocking::run(runtime, value.async_read_frame_chunk());
   BOOST_TEST(first_read.to_vector() == first, boost::test_tools::per_element());
   const auto second_read = fcl::asio::blocking::run(runtime, value.async_read_frame());
   BOOST_TEST(second_read == second, boost::test_tools::per_element());
   BOOST_TEST(backend->chunk_reads == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_session_lifecycle_ignores_stale_replaced_session) {
   struct tracked_session {
      std::uint64_t id = 0;
      node::session_info info;
   };

   const auto remote = peer(2);
   auto sessions = std::map<std::uint64_t, std::shared_ptr<tracked_session>>{};
   auto stale = std::make_shared<tracked_session>(tracked_session{.id = 1, .info = {.remote_peer = remote}});
   auto current = std::make_shared<tracked_session>(tracked_session{.id = 2, .info = {.remote_peer = remote}});

   sessions[current->id] = current;

   BOOST_TEST(!detail::erase_current_session(sessions, stale));
   BOOST_REQUIRE(sessions.contains(current->id));
   BOOST_TEST(sessions.at(current->id).get() == current.get());

   BOOST_TEST(detail::erase_current_session(sessions, current));
   BOOST_TEST(!sessions.contains(current->id));
}

BOOST_AUTO_TEST_CASE(quic_transport_adapter_preserves_endpoint_kind_and_authority) {
   const auto ip4 = fcl::quic::to_transport_endpoint(fcl::quic::endpoint{.host = "127.0.0.1", .port = 4001});
   BOOST_TEST(static_cast<int>(ip4.host_type) == static_cast<int>(fcl::transport::endpoint::host_kind::ip4));
   BOOST_TEST(static_cast<int>(ip4.protocol) == static_cast<int>(fcl::transport::endpoint::protocol_kind::quic_v1));
   const auto authority = std::string{"127.0.0.1"} + ":" + std::to_string(4001);
   BOOST_TEST(ip4.authority() == authority);
   const auto roundtrip = fcl::quic::from_transport_endpoint(ip4);
   BOOST_TEST(roundtrip.authority() == authority);

   const auto dns = fcl::quic::to_transport_endpoint(fcl::quic::endpoint{.host = "localhost", .port = 4002});
   BOOST_TEST(static_cast<int>(dns.host_type) == static_cast<int>(fcl::transport::endpoint::host_kind::dns));
}

BOOST_AUTO_TEST_CASE(p2p_multistream_select_encodes_libp2p_messages) {
   using namespace protocol_negotiation;

   const auto header = encode_frame(encode_message(protocol_negotiation::message{.kind = message_kind::header}));
   BOOST_TEST(header == std::vector<std::uint8_t>({19,  '/', 'm', 'u', 'l', 't', 'i', 's', 't', 'r',
                                                   'e', 'a', 'm', '/', '1', '.', '0', '.', '0', '\n'}),
              boost::test_tools::per_element());

   const auto ping = protocol_id{.value = "/ipfs/ping/1.0.0"};
   auto decoded = decode_message(decode_frame(encode_frame(encode_message(protocol_negotiation::message{
                                                  .kind = message_kind::protocol, .protocol = ping})))
                                     .payload);
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(message_kind::protocol));
   BOOST_TEST(decoded.protocol.value == ping.value);

   auto list = decode_message(encode_message(
       protocol_negotiation::message{.kind = message_kind::protocols, .protocols = std::vector<protocol_id>{ping}}));
   BOOST_TEST(static_cast<int>(list.kind) == static_cast<int>(message_kind::protocols));
   BOOST_REQUIRE_EQUAL(list.protocols.size(), 1U);
   BOOST_TEST(list.protocols.front().value == ping.value);

   BOOST_CHECK_THROW((void)decode_frame(std::vector<std::uint8_t>{0x81, 0x81, 0x01}), fcl::exceptions::base);
   BOOST_CHECK_THROW((void)decode_message(std::vector<std::uint8_t>{'b', 'a', 'd', '\n'}), fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_reachability_relay_protocol_ids_are_exact) {
   BOOST_TEST(builtins::autonat_v1.value == "/libp2p/autonat/1.0.0");
   BOOST_TEST(builtins::autonat_v2_dial_request.value == "/libp2p/autonat/2/dial-request");
   BOOST_TEST(builtins::autonat_v2_dial_back.value == "/libp2p/autonat/2/dial-back");
   BOOST_TEST(builtins::relay_hop.value == "/libp2p/circuit/relay/0.2.0/hop");
   BOOST_TEST(builtins::relay_stop.value == "/libp2p/circuit/relay/0.2.0/stop");
   BOOST_TEST(builtins::dcutr.value == "/libp2p/dcutr");
   BOOST_TEST(builtins::kad_dht.value == "/ipfs/kad/1.0.0");
   BOOST_TEST(builtins::rendezvous.value == "/rendezvous/1.0.0");
   BOOST_TEST(builtins::meshsub_v11.value == "/meshsub/1.1.0");
   BOOST_TEST(builtins::meshsub_v10.value == "/meshsub/1.0.0");
}

BOOST_AUTO_TEST_CASE(p2p_reachability_relay_public_types_are_owner_scoped) {
   static_assert(std::is_same_v<decltype(reachability::result{}.value), reachability::state>);
   static_assert(std::is_same_v<decltype(hole_punch::result{}.value), hole_punch::status>);
   static_assert(std::is_same_v<decltype(relay::reservation::info{}.relay_peer), peer_id>);
   static_assert(std::is_same_v<decltype(path::policy{}.allow_relay), bool>);
   static_assert(std::is_same_v<decltype(path::result{}.kind), path::kind>);
   static_assert(std::is_same_v<decltype(resource_manager::snapshot{}.active_streams), std::size_t>);
   static_assert(std::is_same_v<decltype(dht::options{}.replication), std::size_t>);
   static_assert(std::is_same_v<decltype(dht::query_result{}.target), dht::key>);
   static_assert(std::is_same_v<decltype(rendezvous::registration{}.peer), peer_id>);
   static_assert(std::is_same_v<decltype(discovery::policy{}.enabled), bool>);
   static_assert(std::is_same_v<decltype(discovery::result{}.discovered_by), discovery::source>);
}

BOOST_AUTO_TEST_CASE(p2p_dht_codec_roundtrips_libp2p_message_shape_and_rejects_malformed) {
   const auto provider = peer(90);
   const auto target = peer(91);
   const auto provider_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4900/quic-v1/p2p/" + provider.to_string());
   const auto target_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4901/quic-v1/p2p/" + target.to_string());
   const auto key = make_dht_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't'});

   const auto encoded = dht::codec::encode(dht::message{
       .type = dht::message_type::get_providers,
       .key_value = key,
       .closer_peers = std::vector<dht::peer>{dht::peer{
           .id = target,
           .endpoints = std::vector<endpoint>{target_endpoint},
           .connection = dht::connection_type::can_connect,
       }},
       .provider_peers = std::vector<dht::peer>{dht::peer{
           .id = provider,
           .endpoints = std::vector<endpoint>{provider_endpoint},
           .connection = dht::connection_type::connected,
       }},
   });
   const auto decoded = dht::codec::decode(encoded);

   BOOST_TEST(static_cast<int>(decoded.type) == static_cast<int>(dht::message_type::get_providers));
   BOOST_TEST(decoded.key_value.bytes == key.bytes, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(decoded.closer_peers.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.provider_peers.size(), 1U);
   BOOST_TEST(decoded.closer_peers.front().id.to_string() == target.to_string());
   BOOST_TEST(decoded.closer_peers.front().endpoints.front().to_string() == target_endpoint.to_string());
   BOOST_TEST(static_cast<int>(decoded.closer_peers.front().connection) ==
              static_cast<int>(dht::connection_type::can_connect));
   BOOST_TEST(decoded.provider_peers.front().id.to_string() == provider.to_string());
   BOOST_TEST(decoded.provider_peers.front().endpoints.front().to_string() == provider_endpoint.to_string());
   BOOST_TEST(static_cast<int>(decoded.provider_peers.front().connection) ==
              static_cast<int>(dht::connection_type::connected));

   BOOST_CHECK_THROW((void)dht::codec::decode(std::vector<std::uint8_t>{0x02, 0x08, 0x63}), fcl::exceptions::base);
   BOOST_CHECK_THROW((void)dht::codec::decode(std::vector<std::uint8_t>{0x01, 0x10}), fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_dht_routing_table_uses_sha256_xor_distance_and_bounds_results) {
   const auto local = peer(92);
   const auto first = peer(93);
   const auto second = peer(94);
   auto table = dht::routing_table{local, dht::options{.replication = 1}};
   table.upsert(dht::peer{.id = first});
   table.upsert(dht::peer{.id = second});
   table.upsert(dht::peer{.id = local});

   const auto zero = distance_between(first.to_bytes(), first.to_bytes());
   BOOST_TEST(std::ranges::all_of(zero.bytes, [](auto value) { return value == 0; }));

   const auto closest = table.closest(second.to_bytes(), 20);
   BOOST_REQUIRE_EQUAL(closest.size(), 1U);
   BOOST_TEST(closest.front().id.to_string() == second.to_string());
   BOOST_REQUIRE_EQUAL(table.snapshot().size(), 2U);
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_codec_roundtrips_register_discover_cookie_and_status) {
   const auto opts = rendezvous::options{};
   const auto identity = make_test_identity();
   const auto record = make_signed_rendezvous_peer_record(identity, {}, 42);
   const auto encoded_register = rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::register_peer,
       .register_value =
           rendezvous::register_request{
               .namespace_name = "fcl.discovery",
               .signed_peer_record = record,
               .ttl = std::chrono::seconds{7'200},
           },
   });
   const auto decoded_register = rendezvous::codec::decode(encoded_register, opts);
   BOOST_TEST(static_cast<int>(decoded_register.type) == static_cast<int>(rendezvous::message_type::register_peer));
   BOOST_REQUIRE(decoded_register.register_value.has_value());
   BOOST_TEST(decoded_register.register_value->namespace_name == "fcl.discovery");
   BOOST_TEST(decoded_register.register_value->signed_peer_record == record, boost::test_tools::per_element());
   BOOST_TEST(decoded_register.register_value->ttl == std::chrono::seconds{7'200});

   const auto decoded_peer_record = rendezvous::codec::open_peer_record(signed_envelope::decode(record), identity.peer);
   BOOST_TEST(decoded_peer_record.peer.to_string() == identity.peer.to_string());
   BOOST_REQUIRE_EQUAL(decoded_peer_record.endpoints.size(), 1U);
   BOOST_TEST(decoded_peer_record.sequence == 42U);

   const auto cookie = rendezvous::codec::make_cookie(42, "fcl.discovery");
   BOOST_TEST(rendezvous::codec::read_cookie(cookie) == 42U);
   BOOST_TEST(rendezvous::codec::read_cookie_namespace(cookie) == "fcl.discovery");
   const auto encoded_discover = rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::discover_response,
       .discover_response_value =
           rendezvous::discover_response{
               .registrations = std::vector<rendezvous::registration>{rendezvous::registration{
                   .namespace_name = "fcl.discovery",
                   .signed_peer_record = record,
                   .ttl = std::chrono::seconds{7'200},
               }},
               .cookie = cookie,
               .status_value = rendezvous::status::ok,
           },
   });
   const auto decoded_discover = rendezvous::codec::decode(encoded_discover, opts);
   BOOST_TEST(static_cast<int>(decoded_discover.type) == static_cast<int>(rendezvous::message_type::discover_response));
   BOOST_REQUIRE(decoded_discover.discover_response_value.has_value());
   BOOST_REQUIRE_EQUAL(decoded_discover.discover_response_value->registrations.size(), 1U);
   BOOST_TEST(decoded_discover.discover_response_value->registrations.front().namespace_name == "fcl.discovery");
   BOOST_TEST(decoded_discover.discover_response_value->registrations.front().peer.to_string() ==
              identity.peer.to_string());
   BOOST_TEST(decoded_discover.discover_response_value->registrations.front().signed_peer_record == record,
              boost::test_tools::per_element());
   BOOST_TEST(rendezvous::codec::read_cookie(decoded_discover.discover_response_value->cookie) == 42U);

   BOOST_CHECK_THROW((void)rendezvous::codec::read_cookie(std::vector<std::uint8_t>{1, 2, 3}), fcl::exceptions::base);
   BOOST_CHECK_THROW((void)rendezvous::codec::encode(rendezvous::message{
                         .type = rendezvous::message_type::discover,
                         .discover_value = rendezvous::discover_request{.namespace_name = std::string(300, 'x')},
                     }),
                     fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_codec_roundtrips_v11_rpc_and_rejects_malformed) {
   const auto identity = make_test_identity();
   auto message = pubsub::message{
       .from = identity.peer,
       .data = std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 7},
       .subject = pubsub::topic{.value = "fcl.topic"},
       .key = encode_public_key(identity.key),
   };
   pubsub::codec::sign_message(message, fcl::crypto::pem::read_private_key(identity.private_key_pem));
   BOOST_TEST(pubsub::codec::verify_message(message));

   const auto id = pubsub::codec::message_id(message);
   BOOST_TEST(!id.empty());

   const auto encoded =
       pubsub::codec::encode(
           pubsub::rpc{
               .subscriptions =
                   std::vector<pubsub::subscription>{
                       pubsub::subscription{.subscribe = true, .subject = pubsub::topic{.value = "fcl.topic"}},
                       pubsub::subscription{.subscribe = false, .subject = pubsub::topic{.value = "fcl.old"}},
                   },
               .messages = std::vector<pubsub::message>{message},
               .control_value =
                   pubsub::control{
                       .have = std::vector<pubsub::control::ihave>{pubsub::control::ihave{
                           .subject = pubsub::topic{.value = "fcl.topic"},
                           .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                       .want = std::vector<pubsub::control::iwant>{pubsub::control::iwant{
                           .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                       .grafts = std::vector<pubsub::control::graft>{pubsub::control::graft{
                           .subject = pubsub::topic{.value = "fcl.topic"}}},
                       .prunes = std::vector<pubsub::control::prune>{pubsub::control::prune{
                           .subject = pubsub::topic{.value = "fcl.topic"},
                           .peers = std::vector<pubsub::peer_info>{pubsub::peer_info{.peer = identity.peer}},
                           .backoff = std::chrono::seconds{60},
                       }},
                   },
           });

   const auto decoded = pubsub::codec::decode(encoded, pubsub::options{});
   BOOST_REQUIRE_EQUAL(decoded.subscriptions.size(), 2U);
   BOOST_TEST(decoded.subscriptions.front().subscribe);
   BOOST_TEST(decoded.subscriptions.front().subject.value == "fcl.topic");
   BOOST_REQUIRE_EQUAL(decoded.messages.size(), 1U);
   BOOST_TEST(decoded.messages.front().data == message.data, boost::test_tools::per_element());
   BOOST_TEST(pubsub::codec::verify_message(decoded.messages.front()));
   BOOST_REQUIRE(decoded.control_value.has_value());
   BOOST_REQUIRE_EQUAL(decoded.control_value->have.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.control_value->want.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.control_value->grafts.size(), 1U);
   BOOST_REQUIRE_EQUAL(decoded.control_value->prunes.size(), 1U);
   BOOST_TEST(decoded.control_value->prunes.front().backoff == std::chrono::seconds{60});

   BOOST_CHECK_THROW((void)pubsub::codec::decode(std::vector<std::uint8_t>{0xff, 0xff, 0xff, 0xff}, pubsub::options{}),
                     fcl::exceptions::base);
   auto strict = pubsub::options{};
   strict.limits.max_rpc_size = 4;
   BOOST_CHECK_THROW((void)pubsub::codec::decode(encoded, strict), fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_signing_rejects_tampered_payload) {
   const auto identity = make_test_identity();
   auto message = pubsub::message{
       .from = identity.peer,
       .data = std::vector<std::uint8_t>{'s', 'i', 'g', 'n', 'e', 'd'},
       .seqno = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 1},
       .subject = pubsub::topic{.value = "fcl.signed"},
       .key = encode_public_key(identity.key),
   };
   pubsub::codec::sign_message(message, fcl::crypto::pem::read_private_key(identity.private_key_pem));
   BOOST_TEST(pubsub::codec::verify_message(message));

   message.data.push_back('!');
   BOOST_TEST(!pubsub::codec::verify_message(message));
}

BOOST_AUTO_TEST_CASE(p2p_signed_envelope_seals_and_verifies_domain_payload_and_signer) {
   const auto identity = make_test_identity();
   const auto payload_type = fcl::multiformats::varint_encode(0x0302);
   const auto payload = std::vector<std::uint8_t>{1, 2, 3, 4, 5};

   const auto envelope =
       signed_envelope::seal(identity.key, fcl::crypto::pem::read_private_key(identity.private_key_pem),
                             "libp2p-relay-rsvp", payload_type, payload);
   const auto encoded = envelope.encode();
   const auto decoded = signed_envelope::decode(encoded);

   BOOST_TEST(decoded.payload_type == payload_type, boost::test_tools::per_element());
   BOOST_TEST(decoded.payload == payload, boost::test_tools::per_element());
   BOOST_TEST(decoded.signer().to_string() == identity.peer.to_string());
   BOOST_CHECK_NO_THROW(decoded.verify("libp2p-relay-rsvp", identity.peer));
   BOOST_CHECK_THROW(decoded.verify("wrong-domain", identity.peer), fcl::exceptions::base);

   auto tampered = decoded;
   tampered.payload.back() ^= 0x01U;
   BOOST_CHECK_THROW(tampered.verify("libp2p-relay-rsvp", identity.peer), fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_relay_voucher_uses_signed_envelope_and_rejects_stale_or_wrong_signer) {
   const auto relay_identity = make_test_identity();
   const auto other_identity = make_test_identity();
   const auto reservation = relay::voucher{
       .relay_peer = relay_identity.peer,
       .peer = peer(44),
       .expires_at = 4'102'444'800ULL,
   };

   const auto envelope = relay::codec::seal_reservation_voucher(
       reservation, relay_identity.key, fcl::crypto::pem::read_private_key(relay_identity.private_key_pem));
   const auto decoded = relay::codec::open_reservation_voucher(envelope, relay_identity.peer, 4'102'444'799ULL);

   BOOST_TEST(decoded.relay_peer.to_string() == reservation.relay_peer.to_string());
   BOOST_TEST(decoded.peer.to_string() == reservation.peer.to_string());
   BOOST_TEST(decoded.expires_at == reservation.expires_at);

   BOOST_CHECK_THROW(
       (void)relay::codec::open_reservation_voucher(envelope, relay_identity.peer, reservation.expires_at),
       fcl::exceptions::base);
   BOOST_CHECK_THROW((void)relay::codec::open_reservation_voucher(envelope, other_identity.peer, 4'102'444'799ULL),
                     fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_resource_manager_enforces_relay_stream_and_byte_limits) {
   auto manager = resource_manager{resource_manager::limits{
       .max_streams = 2,
       .max_relay_streams = 1,
       .max_relay_bytes = 8,
   }};
   BOOST_TEST(manager.try_acquire_relay_stream());
   BOOST_TEST(!manager.try_acquire_relay_stream());
   BOOST_TEST(manager.add_relay_bytes(4));
   BOOST_TEST(!manager.add_relay_bytes(5));
   auto snapshot = manager.current();
   BOOST_TEST(snapshot.active_streams == 1U);
   BOOST_TEST(snapshot.active_relay_streams == 1U);
   BOOST_TEST(snapshot.relay_bytes == 4U);
   BOOST_TEST(snapshot.denied == 2U);
   manager.release_relay_stream();
   snapshot = manager.current();
   BOOST_TEST(snapshot.active_streams == 0U);
   BOOST_TEST(snapshot.active_relay_streams == 0U);
}

BOOST_AUTO_TEST_CASE(p2p_relay_accounting_validates_reservation_before_global_charge) {
   struct fake_resources {
      std::uint64_t max_relay_bytes = 8;
      std::uint64_t relay_bytes = 0;

      bool add_relay_bytes(std::uint64_t bytes) noexcept {
         if (bytes > max_relay_bytes || relay_bytes > max_relay_bytes - bytes) {
            return false;
         }
         relay_bytes += bytes;
         return true;
      }
   };
   struct fake_metrics {
      std::uint64_t relay_bytes = 0;
      std::uint64_t relay_rejections = 0;
   };
   struct fake_reservation {
      std::uint64_t max_bytes = 4;
      std::uint64_t bytes = 0;
   };

   auto resources = fake_resources{};
   auto metrics = fake_metrics{};
   auto reservation = fake_reservation{};
   BOOST_TEST(!detail::add_relay_bytes(resources, metrics, &reservation, true, 5));
   BOOST_TEST(resources.relay_bytes == 0U);
   BOOST_TEST(metrics.relay_bytes == 0U);
   BOOST_TEST(metrics.relay_rejections == 1U);
   BOOST_TEST(reservation.bytes == 0U);

   BOOST_TEST(detail::add_relay_bytes(resources, metrics, &reservation, true, 2));
   BOOST_TEST(detail::add_relay_bytes(resources, metrics, &reservation, true, 2));
   BOOST_TEST(resources.relay_bytes == 4U);
   BOOST_TEST(metrics.relay_bytes == 4U);
   BOOST_TEST(reservation.bytes == 4U);
   BOOST_TEST(!detail::add_relay_bytes(resources, metrics, &reservation, true, 1));
   BOOST_TEST(resources.relay_bytes == 4U);
   BOOST_TEST(metrics.relay_bytes == 4U);
   BOOST_TEST(metrics.relay_rejections == 2U);

   BOOST_TEST(!detail::add_relay_bytes(resources, metrics, static_cast<fake_reservation*>(nullptr), true, 1));
   BOOST_TEST(resources.relay_bytes == 4U);
   BOOST_TEST(metrics.relay_bytes == 4U);
   BOOST_TEST(metrics.relay_rejections == 3U);
}

BOOST_AUTO_TEST_CASE(p2p_resource_manager_enforces_peer_protocol_dial_and_reservation_scopes) {
   auto manager = resource_manager{resource_manager::limits{
       .max_streams = 4,
       .max_streams_per_peer = 1,
       .max_streams_per_protocol = 1,
       .max_relay_reservations = 1,
       .max_dial_attempts_per_peer = 1,
       .max_malformed_messages_per_peer = 1,
   }};
   const auto scope = resource_manager::scope{.peer = peer(93), .protocol = builtins::relay_hop};
   const auto other = resource_manager::scope{.peer = peer(94), .protocol = builtins::relay_hop};

   BOOST_TEST(manager.try_acquire_stream(scope));
   BOOST_TEST(!manager.try_acquire_stream(scope));
   manager.release_stream(scope);
   BOOST_TEST(manager.try_acquire_stream(scope));
   manager.release_stream(scope);

   BOOST_TEST(manager.try_acquire_stream(other));
   BOOST_TEST(!manager.try_acquire_stream(scope));
   manager.release_stream(other);

   BOOST_TEST(manager.try_acquire_relay_reservation(scope));
   BOOST_TEST(!manager.try_acquire_relay_reservation(
       resource_manager::scope{.peer = peer(95), .protocol = builtins::relay_hop}));
   manager.release_relay_reservation(scope);

   BOOST_TEST(manager.try_acquire_dial(scope));
   BOOST_TEST(!manager.try_acquire_dial(scope));
   BOOST_TEST(manager.record_malformed(scope));
   BOOST_TEST(!manager.record_malformed(scope));

   const auto snapshot = manager.current();
   BOOST_TEST(snapshot.denied >= 4U);
}

BOOST_AUTO_TEST_CASE(p2p_resource_manager_enforces_connection_session_scopes) {
   auto manager = resource_manager{resource_manager::limits{
       .max_pending_inbound_sessions = 1,
       .max_pending_outbound_sessions = 1,
       .max_inbound_sessions = 1,
       .max_outbound_sessions = 1,
       .max_sessions_per_peer = 1,
   }};
   const auto inbound = resource_manager::session_scope{
       .peer = peer(231),
       .direction = resource_manager::session_direction::inbound,
   };
   const auto outbound = resource_manager::session_scope{
       .peer = peer(232),
       .direction = resource_manager::session_direction::outbound,
   };

   BOOST_TEST(manager.try_acquire_pending_session(resource_manager::session_direction::inbound));
   BOOST_TEST(!manager.try_acquire_pending_session(resource_manager::session_direction::inbound));
   manager.release_pending_session(resource_manager::session_direction::inbound);
   BOOST_TEST(manager.try_acquire_pending_session(resource_manager::session_direction::outbound));
   BOOST_TEST(!manager.try_acquire_pending_session(resource_manager::session_direction::outbound));
   manager.release_pending_session(resource_manager::session_direction::outbound);

   BOOST_TEST(manager.try_acquire_session(inbound));
   BOOST_TEST(!manager.try_acquire_session(inbound));
   BOOST_TEST(manager.try_acquire_session(outbound));
   manager.release_session(inbound);
   manager.release_session(outbound);

   const auto snapshot = manager.current();
   BOOST_TEST(snapshot.active_inbound_sessions == 0U);
   BOOST_TEST(snapshot.active_outbound_sessions == 0U);
   BOOST_TEST(snapshot.pending_inbound_sessions == 0U);
   BOOST_TEST(snapshot.pending_outbound_sessions == 0U);
   BOOST_TEST(snapshot.denied >= 3U);
}

BOOST_AUTO_TEST_CASE(p2p_node_peer_protection_api_is_tagged_and_additive) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(233))};
   const auto protected_peer = peer(234);

   BOOST_TEST(!value.is_peer_protected(protected_peer));
   value.protect_peer(protected_peer, "bootstrap");
   BOOST_TEST(value.is_peer_protected(protected_peer));
   BOOST_TEST(value.unprotect_peer(protected_peer, "other"));
   BOOST_TEST(value.is_peer_protected(protected_peer));
   BOOST_TEST(!value.unprotect_peer(protected_peer, "bootstrap"));
   BOOST_TEST(!value.is_peer_protected(protected_peer));

   fcl::asio::blocking::run(runtime, value.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_prunes_unprotected_sessions_and_keeps_protected_peer) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(235));
   client_options.limits.max_sessions = 2;
   client_options.limits.max_outbound_sessions = 2;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(236))};
   auto second = node{runtime, options_for(peer(237))};
   auto third = node{runtime, options_for(peer(238))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);
   register_echo(second);
   register_echo(third);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   const auto third_endpoint = listen(third, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   client.protect_peer(first.local_peer(), "bootstrap");
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));

   auto metrics = client.metrics();
   BOOST_TEST(metrics.active_sessions == 2U);
   BOOST_TEST(metrics.sessions_pruned >= 1U);
   BOOST_TEST(client.is_peer_protected(first.local_peer()));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(first.local_peer(), builtins::echo,
                                                                           node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'p', 'r', 'o', 't', 'e', 'c', 't'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, third.async_stop());
   fcl::asio::blocking::run(runtime, second.async_stop());
   fcl::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_prunes_batch_to_low_watermark) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 6}};
   auto client_options = options_for(peer(60));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 4;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{60'000};

   auto first = node{runtime, options_for(peer(61))};
   auto second = node{runtime, options_for(peer(62))};
   auto third = node{runtime, options_for(peer(63))};
   auto fourth = node{runtime, options_for(peer(64))};
   auto fifth = node{runtime, options_for(peer(65))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(fifth);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   const auto third_endpoint = listen(third, runtime);
   const auto fourth_endpoint = listen(fourth, runtime);
   const auto fifth_endpoint = listen(fifth, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(fourth_endpoint, node::connect_options{.expected_peer = fourth.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(fifth_endpoint, node::connect_options{.expected_peer = fifth.local_peer()}));

   auto metrics = client.metrics();
   BOOST_TEST(metrics.active_sessions == 2U);
   BOOST_TEST(metrics.sessions_pruned >= 3U);

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(fifth.local_peer(), builtins::echo,
                                                                           node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'b', 'a', 't', 'c', 'h'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, fifth.async_stop());
   fcl::asio::blocking::run(runtime, fourth.async_stop());
   fcl::asio::blocking::run(runtime, third.async_stop());
   fcl::asio::blocking::run(runtime, second.async_stop());
   fcl::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_rejects_when_all_sessions_are_protected) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(239));
   client_options.limits.max_sessions = 2;
   client_options.limits.max_outbound_sessions = 2;
   client_options.limits.session_low_watermark = 1;
   client_options.limits.session_grace_period = std::chrono::milliseconds{0};
   client_options.limits.session_prune_silence = std::chrono::milliseconds{1};

   auto first = node{runtime, options_for(peer(240))};
   auto second = node{runtime, options_for(peer(241))};
   auto third = node{runtime, options_for(peer(242))};
   auto client = node{runtime, std::move(client_options)};

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   const auto third_endpoint = listen(third, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   client.protect_peer(first.local_peer(), "bootstrap");
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
   client.protect_peer(second.local_peer(), "bootstrap");

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(third_endpoint, node::connect_options{.expected_peer = third.local_peer()}));
      BOOST_FAIL("expected all-protected session limit rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   const auto metrics = client.metrics();
   BOOST_TEST(metrics.active_sessions == 2U);
   BOOST_TEST(metrics.connection_rejections >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, third.async_stop());
   fcl::asio::blocking::run(runtime, second.async_stop());
   fcl::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_enforces_outbound_session_limit) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(243));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 1;
   client_options.limits.session_low_watermark = 4;
   auto first = node{runtime, options_for(peer(244))};
   auto second = node{runtime, options_for(peer(245))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(first);

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(first_endpoint, node::connect_options{.expected_peer = first.local_peer()}));
   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(second_endpoint, node::connect_options{.expected_peer = second.local_peer()}));
      BOOST_FAIL("expected outbound session limit rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(first.local_peer(), builtins::echo,
                                                                           node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'l', 'i', 'm', 'i', 't'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().connection_rejections >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, second.async_stop());
   fcl::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_allows_bounded_parallel_sessions_per_peer) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(250));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 4;
   client_options.limits.max_sessions_per_peer = 2;
   client_options.limits.session_low_watermark = 4;
   auto server = node{runtime, options_for(peer(251))};
   auto client = node{runtime, std::move(client_options)};
   register_echo(server);

   const auto endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(client.metrics().active_sessions == 2U);

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                                           node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'p', 'a', 'r', 'a', 'l', 'l', 'e', 'l'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_enforces_sessions_per_peer_limit) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto client_options = options_for(peer(252));
   client_options.limits.max_sessions = 4;
   client_options.limits.max_outbound_sessions = 4;
   client_options.limits.max_sessions_per_peer = 1;
   client_options.limits.session_low_watermark = 4;
   auto server = node{runtime, options_for(peer(253))};
   auto client = node{runtime, std::move(client_options)};

   const auto endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(endpoint, node::connect_options{.expected_peer = server.local_peer()}));
      BOOST_FAIL("expected per-peer session limit rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }
   BOOST_TEST(client.metrics().active_sessions == 1U);
   BOOST_TEST(client.metrics().connection_rejections >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connection_manager_rejects_pending_outbound_limit_without_killing_first_attempt) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto client_options = options_for(peer(246));
   client_options.limits.max_pending_outbound_sessions = 1;
   auto client = node{runtime, std::move(client_options)};
   const auto stalled_endpoint = start_stalling_tcp_peer(runtime, std::chrono::milliseconds{500});

   auto first = boost::asio::co_spawn(
       runtime.context(),
       client.async_connect(stalled_endpoint,
                            node::connect_options{
                                .expected_peer = peer(247),
                                .allow_relay = false,
                                .timeout = std::chrono::milliseconds{500},
                            }),
       boost::asio::use_future);
   wait_on_runtime(runtime, std::chrono::milliseconds{50}, "pending outbound admission");

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(stalled_endpoint,
                                       node::connect_options{
                                           .expected_peer = peer(247),
                                           .allow_relay = false,
                                           .timeout = std::chrono::milliseconds{100},
                                       }));
      BOOST_FAIL("expected pending outbound session limit rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::backpressure_rejected));
   }

   try {
      (void)first.get();
   } catch (const fcl::exceptions::base&) {
   }
   BOOST_TEST(client.metrics().connection_rejections >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_relay_hop_codec_matches_spec_shape) {
   auto reserve = relay::hop_message{.kind = relay::hop_message::message_kind::reserve};
   BOOST_TEST(relay::codec::encode_hop(reserve) == std::vector<std::uint8_t>({0x02, 0x08, 0x00}),
              boost::test_tools::per_element());

   auto status = relay::hop_message{
       .kind = relay::hop_message::message_kind::status,
       .status = relay::status::ok,
   };
   BOOST_TEST(relay::codec::encode_hop(status) == std::vector<std::uint8_t>({0x04, 0x08, 0x02, 0x28, 0x64}),
              boost::test_tools::per_element());
   auto decoded = relay::codec::decode_hop(relay::codec::encode_hop(status));
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(relay::hop_message::message_kind::status));
   BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(relay::status::ok));
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_relay_wire_roundtrips_statuses_limits_and_voucher) {
   const auto identity = make_test_identity();
   const auto relay_peer = identity.peer;
   const auto target_peer = peer(97);
   const auto relay_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4103/quic-v1/p2p/" + relay_peer.to_string());
   const auto voucher = relay::codec::seal_reservation_voucher(
       relay::voucher{
           .relay_peer = relay_peer,
           .peer = target_peer,
           .expires_at = 1'777'000'000,
       },
       identity.key, fcl::crypto::pem::read_private_key(identity.private_key_pem));

   auto decoded_voucher = relay::codec::open_reservation_voucher(voucher, relay_peer, 1'776'999'999);
   BOOST_TEST(decoded_voucher.relay_peer.to_string() == relay_peer.to_string());
   BOOST_TEST(decoded_voucher.peer.to_string() == target_peer.to_string());
   BOOST_TEST(decoded_voucher.expires_at == 1'777'000'000ULL);

   for (auto status :
        {relay::status::ok, relay::status::reservation_refused, relay::status::resource_limit_exceeded,
         relay::status::permission_denied, relay::status::connection_failed, relay::status::no_reservation,
         relay::status::malformed_message, relay::status::unexpected_message}) {
      auto decoded = relay::codec::decode_hop(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .reservation_value =
              relay::reservation{
                  .expires_at = 1'777'000'000,
                  .relay_endpoints = std::vector<endpoint>{relay_endpoint},
                  .voucher = voucher,
              },
          .limit_value = relay::limit{.duration = std::chrono::seconds{60}, .data = 4096},
          .status = status,
      }));
      BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(relay::hop_message::message_kind::status));
      BOOST_TEST(static_cast<int>(decoded.status) == static_cast<int>(status));
      BOOST_REQUIRE(decoded.limit_value.has_value());
      BOOST_TEST(decoded.limit_value->duration == std::chrono::seconds{60});
      BOOST_TEST(decoded.limit_value->data == 4096U);
      if (status == relay::status::ok) {
         BOOST_REQUIRE(decoded.reservation_value.has_value());
         BOOST_REQUIRE(decoded.reservation_value->voucher.has_value());
         BOOST_TEST(decoded.reservation_value->voucher->encode() == voucher.encode(), boost::test_tools::per_element());
      }
   }

   BOOST_CHECK_THROW((void)relay::codec::decode_hop(std::vector<std::uint8_t>{0x02, 0x28, 0x64}),
                     fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_dcutr_codec_matches_spec_shape) {
   auto connect = hole_punch::message{.kind = hole_punch::message::message_kind::connect};
   BOOST_TEST(hole_punch::codec::encode(connect) == std::vector<std::uint8_t>({0x02, 0x08, 0x64}),
              boost::test_tools::per_element());

   auto decoded = hole_punch::codec::decode(hole_punch::codec::encode(hole_punch::message{
       .kind = hole_punch::message::message_kind::sync,
   }));
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(hole_punch::message::message_kind::sync));
}

BOOST_AUTO_TEST_CASE(p2p_dcutr_attempt_tracks_rtt_retry_and_inflight_state) {
   auto attempt = hole_punch::attempt{};
   attempt.peer = peer(98);
   attempt.relay_peer = peer(99);
   attempt.rtt = std::chrono::milliseconds{80};
   attempt.max_attempts = 2;
   BOOST_TEST(attempt.sync_delay() == std::chrono::milliseconds{40});
   BOOST_TEST(attempt.try_begin());
   BOOST_TEST(!attempt.try_begin());
   attempt.finish(hole_punch::status::failed);
   BOOST_TEST(attempt.can_retry());
   BOOST_TEST(attempt.try_begin());
   attempt.finish(hole_punch::status::succeeded);
   BOOST_TEST(!attempt.can_retry());
   BOOST_TEST(static_cast<int>(attempt.result().value) == static_cast<int>(hole_punch::status::succeeded));
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_autonat_v1_codec_matches_spec_shape) {
   const auto id = peer(91);
   const auto endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4101/quic-v1/p2p/" + id.to_string());
   auto dial = reachability::message{
       .kind = reachability::message::message_kind::dial,
       .peer =
           reachability::peer_info{
               .peer = id,
               .endpoints = std::vector<fcl::p2p::endpoint>{endpoint},
           },
   };

   auto decoded = reachability::codec::decode_v1(reachability::codec::encode_v1(dial));
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(reachability::message::message_kind::dial));
   BOOST_REQUIRE(decoded.peer.has_value());
   BOOST_TEST(decoded.peer->peer.to_string() == id.to_string());
   BOOST_REQUIRE_EQUAL(decoded.peer->endpoints.size(), 1U);
   BOOST_TEST(decoded.peer->endpoints.front().to_string() == endpoint.to_string());

   auto response = reachability::codec::decode_v1(reachability::codec::encode_v1(reachability::message{
       .kind = reachability::message::message_kind::dial_response,
       .response =
           reachability::dial_response{
               .status = reachability::dial_status::ok,
               .endpoint = endpoint,
           },
   }));
   BOOST_REQUIRE(response.response.has_value());
   BOOST_TEST(static_cast<int>(response.response->status) == static_cast<int>(reachability::dial_status::ok));
   BOOST_REQUIRE(response.response->endpoint.has_value());
   BOOST_TEST(response.response->endpoint->to_string() == endpoint.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_autonat_v2_codec_covers_nonce_data_and_statuses) {
   const auto id = peer(92);
   const auto remote_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4102/quic-v1/p2p/" + id.to_string());
   auto request = reachability::v2::message{
       .type = reachability::v2::message::kind::dial_request,
       .dial_request =
           reachability::v2::dial_request{
               .endpoints = std::vector<fcl::p2p::endpoint>{remote_endpoint},
               .nonce = 0x0102'0304'0506'0708ULL,
           },
   };

   auto decoded = reachability::codec::decode_v2(reachability::codec::encode_v2(request));
   BOOST_TEST(static_cast<int>(decoded.type) == static_cast<int>(reachability::v2::message::kind::dial_request));
   BOOST_REQUIRE(decoded.dial_request.has_value());
   BOOST_TEST(decoded.dial_request->nonce == 0x0102'0304'0506'0708ULL);
   BOOST_REQUIRE_EQUAL(decoded.dial_request->endpoints.size(), 1U);
   BOOST_TEST(decoded.dial_request->endpoints.front().to_string() == remote_endpoint.to_string());

   auto data_request = reachability::codec::decode_v2(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_data_request,
       .dial_data_request = reachability::v2::dial_data_request{.index = 1, .bytes = 30 * 1024},
   }));
   BOOST_REQUIRE(data_request.dial_data_request.has_value());
   BOOST_TEST(data_request.dial_data_request->index == 1U);
   BOOST_TEST(data_request.dial_data_request->bytes == 30U * 1024U);

   auto data_response = reachability::codec::decode_v2(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_data_response,
       .dial_data_response = reachability::v2::dial_data_response{.data = std::vector<std::uint8_t>(4096, 0x5a)},
   }));
   BOOST_REQUIRE(data_response.dial_data_response.has_value());
   BOOST_TEST(data_response.dial_data_response->data.size() == 4096U);

   auto response = reachability::codec::decode_v2(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_response,
       .dial_response =
           reachability::v2::dial_response{
               .status = reachability::v2::response_status::ok,
               .index = 1,
               .dial_status = reachability::v2::dial_status::dial_back_error,
           },
   }));
   BOOST_REQUIRE(response.dial_response.has_value());
   BOOST_TEST(static_cast<int>(response.dial_response->status) ==
              static_cast<int>(reachability::v2::response_status::ok));
   BOOST_TEST(response.dial_response->index == 1U);
   BOOST_TEST(static_cast<int>(response.dial_response->dial_status) ==
              static_cast<int>(reachability::v2::dial_status::dial_back_error));

   auto dial_back = reachability::codec::decode_v2_dial_back(
       reachability::codec::encode_v2_dial_back(reachability::v2::dial_back{.nonce = request.dial_request->nonce}));
   BOOST_TEST(dial_back.nonce == request.dial_request->nonce);

   auto dial_back_response =
       reachability::codec::decode_v2_dial_back_response(reachability::codec::encode_v2_dial_back_response(
           reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
   BOOST_TEST(static_cast<int>(dial_back_response.status) == static_cast<int>(reachability::v2::dial_back_status::ok));
}

BOOST_AUTO_TEST_CASE(p2p_libp2p_autonat_v2_rejects_oversized_data_response_and_unknown_status) {
   BOOST_CHECK_THROW(
       (void)reachability::codec::encode_v2(reachability::v2::message{
           .type = reachability::v2::message::kind::dial_data_response,
           .dial_data_response = reachability::v2::dial_data_response{.data = std::vector<std::uint8_t>(4097, 0x42)},
       }),
       fcl::exceptions::base);

   // Message { dialResponse: DialResponse { status: 999 } }
   BOOST_CHECK_THROW(
       (void)reachability::codec::decode_v2(std::vector<std::uint8_t>{0x06, 0x12, 0x04, 0x08, 0xe7, 0x07}),
       fcl::exceptions::base);
}

BOOST_AUTO_TEST_CASE(p2p_autonat_v2_probe_public_and_persists_observation) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto observer =
       node{runtime, options_for(peer(100), capability_set{.bits = capabilities::direct_quic | capabilities::autonat})};
   auto subject =
       node{runtime, options_for(peer(101), capability_set{.bits = capabilities::direct_quic | capabilities::autonat})};

   const auto observer_endpoint = listen(observer, runtime);
   (void)listen(subject, runtime);
   subject.peers().learn_endpoint(observer.local_peer(), observer_endpoint,
                                  capability_set{.bits = capabilities::direct_quic | capabilities::autonat});

   const auto state = fcl::asio::blocking::run(runtime, subject.async_probe_reachability(observer.local_peer()));
   BOOST_TEST(static_cast<int>(state) == static_cast<int>(reachability::state::publicly_reachable));

   const auto stored = subject.peers().find(subject.local_peer());
   BOOST_REQUIRE(stored.has_value());
   BOOST_TEST(static_cast<int>(stored->reachability) == static_cast<int>(reachability::state::publicly_reachable));
   BOOST_REQUIRE(stored->observed_endpoint.has_value());

   fcl::asio::blocking::run(runtime, subject.async_stop());
   fcl::asio::blocking::run(runtime, observer.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_reservation_persists_candidate) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto relay_node =
       node{runtime, options_for(peer(102), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client = node{runtime, options_for(peer(103), capability_set{.bits = capabilities::direct_quic |
                                                                             capabilities::relay_reservation})};

   const auto relay_endpoint = listen(relay_node, runtime);
   client.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   const auto info = fcl::asio::blocking::run(runtime, client.async_reserve_relay(relay_node.local_peer()));
   BOOST_TEST(info.relay_peer.to_string() == relay_node.local_peer().to_string());
   BOOST_TEST(!info.voucher.has_value());

   const auto stored = client.peers().find(relay_node.local_peer());
   BOOST_REQUIRE(stored.has_value());
   BOOST_REQUIRE_EQUAL(stored->relay_reservations.size(), 1U);
   BOOST_TEST(stored->relay_reservations.front().relay.to_string() == relay_node.local_peer().to_string());
   BOOST_TEST(stored->relay_reservations.front().voucher.empty());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_reserves_peer_store_candidate) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto relay_node =
       node{runtime, options_for(peer(104), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(105), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 2;
   client_options.relay_policy.max_parallel_reservations = 1;
   auto client = node{runtime, std::move(client_options)};

   const auto relay_endpoint = listen(relay_node, runtime);
   client.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   const auto reservations = fcl::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(reservations.front().relay_peer.to_string() == relay_node.local_peer().to_string());

   const auto stored = client.peers().find(relay_node.local_peer());
   BOOST_REQUIRE(stored.has_value());
   BOOST_REQUIRE_EQUAL(stored->relay_reservations.size(), 1U);
   BOOST_TEST(stored->relay_reservations.front().relay.to_string() == relay_node.local_peer().to_string());
   BOOST_TEST(client.metrics().relay_discovery_refreshes == 1U);
   BOOST_TEST(client.metrics().relay_discovery_attempts == 1U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_backs_off_failed_candidate_and_tries_next) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto bad_options =
       options_for(peer(106), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                     capabilities::relay_reservation});
   bad_options.relay_policy.service_enabled = false;
   auto bad_relay = node{runtime, std::move(bad_options)};
   auto good_relay =
       node{runtime, options_for(peer(107), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(108), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 2;
   client_options.relay_policy.max_parallel_reservations = 1;
   client_options.relay_policy.candidate_backoff = std::chrono::seconds{30};
   auto client = node{runtime, std::move(client_options)};

   const auto bad_endpoint = listen(bad_relay, runtime);
   const auto good_endpoint = listen(good_relay, runtime);
   client.peers().learn_endpoint(
       bad_relay.local_peer(), bad_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   client.peers().learn_endpoint(
       good_relay.local_peer(), good_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   auto bad_record = client.peers().find(bad_relay.local_peer()).value();
   bad_record.score = 100.0;
   client.peers().upsert(std::move(bad_record));

   const auto reservations = fcl::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(reservations.front().relay_peer.to_string() == good_relay.local_peer().to_string());
   BOOST_TEST(client.metrics().relay_discovery_attempts == 2U);
   BOOST_TEST(client.metrics().relay_discovery_failures == 1U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 1U);

   const auto failed = client.peers().find(bad_relay.local_peer());
   BOOST_REQUIRE(failed.has_value());
   BOOST_TEST(failed->discovery_backoff_until > std::chrono::system_clock::now());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, good_relay.async_stop());
   fcl::asio::blocking::run(runtime, bad_relay.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_accepts_dht_and_rendezvous_sourced_candidates) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto dht_relay =
       node{runtime, options_for(peer(116), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto rendezvous_relay =
       node{runtime, options_for(peer(117), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(120), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 2;
   client_options.relay_policy.max_candidates_per_refresh = 2;
   client_options.relay_policy.max_parallel_reservations = 1;
   auto client = node{runtime, std::move(client_options)};

   const auto dht_endpoint = listen(dht_relay, runtime);
   const auto rendezvous_endpoint = listen(rendezvous_relay, runtime);
   client.peers().upsert(peer_store::record{
       .peer = dht_relay.local_peer(),
       .capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                              capabilities::relay_reservation},
       .discovered_by = discovery::source::dht,
       .endpoints = std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{.endpoint = dht_endpoint}},
       .discovery_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5},
   });
   client.peers().upsert(peer_store::record{
       .peer = rendezvous_relay.local_peer(),
       .capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                              capabilities::relay_reservation},
       .discovered_by = discovery::source::rendezvous,
       .endpoints =
           std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{.endpoint = rendezvous_endpoint}},
       .discovery_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5},
   });

   const auto reservations = fcl::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 2U);
   BOOST_TEST(client.metrics().relay_discovery_attempts == 2U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 2U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, rendezvous_relay.async_stop());
   fcl::asio::blocking::run(runtime, dht_relay.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_autorelay_refresh_respects_candidate_and_target_limits) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto first =
       node{runtime, options_for(peer(113), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto second =
       node{runtime, options_for(peer(114), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto client_options =
       options_for(peer(115), capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 1;
   client_options.relay_policy.max_parallel_reservations = 1;
   auto client = node{runtime, std::move(client_options)};

   const auto first_endpoint = listen(first, runtime);
   const auto second_endpoint = listen(second, runtime);
   client.peers().learn_endpoint(
       first.local_peer(), first_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   client.peers().learn_endpoint(
       second.local_peer(), second_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   const auto reservations = fcl::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(client.metrics().relay_discovery_attempts == 1U);
   BOOST_TEST(client.metrics().relay_discovery_successes == 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, second.async_stop());
   fcl::asio::blocking::run(runtime, first.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_fallback_refreshes_candidate_without_explicit_relay_peer) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("autorelay-fallback-relay");
   const auto source_identity = make_test_certificate_identity("autorelay-fallback-source");
   const auto target_identity = make_test_certificate_identity("autorelay-fallback-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source_options =
       options_for(source_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay_reservation});
   source_options.relay_policy.target_reservations = 1;
   source_options.relay_policy.max_candidates_per_refresh = 2;
   source_options.relay_policy.max_parallel_reservations = 1;
   auto source = node{runtime, std::move(source_options)};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)fcl::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   auto stream = fcl::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'a', 'u', 't', 'o'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(source.metrics().relay_discovery_refreshes >= 1U);
   BOOST_TEST(source.metrics().relay_discovery_successes >= 1U);
   BOOST_TEST(source.metrics().path_relay_opens >= 1U);

   fcl::asio::blocking::run(runtime, target.async_stop());
   fcl::asio::blocking::run(runtime, source.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_policy_options_are_behavioral) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto relay_options = options_for(peer(110), capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                      capabilities::relay_reservation});
   relay_options.relay_policy.service_enabled = false;
   auto relay_node = node{runtime, std::move(relay_options)};
   auto client = node{runtime, options_for(peer(111), capability_set{.bits = capabilities::direct_quic |
                                                                             capabilities::relay_reservation})};

   const auto relay_endpoint = listen(relay_node, runtime);
   client.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});

   try {
      (void)fcl::asio::blocking::run(runtime, client.async_reserve_relay(relay_node.local_peer()));
      BOOST_FAIL("expected relay service policy rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::relay_rejected));
   }

   auto disabled_client_options = options_for(peer(112), capability_set{.bits = capabilities::direct_quic});
   disabled_client_options.relay_policy.client_enabled = false;
   auto disabled_client = node{runtime, std::move(disabled_client_options)};
   try {
      (void)fcl::asio::blocking::run(runtime, disabled_client.async_reserve_relay(relay_node.local_peer()));
      BOOST_FAIL("expected relay client policy rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::relay_not_available));
   }

   fcl::asio::blocking::run(runtime, disabled_client.async_stop());
   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_connect_requires_target_reservation) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-reservation-owner-relay");
   const auto source_identity = make_test_certificate_identity("relay-reservation-owner-source");
   const auto target_identity = make_test_certificate_identity("relay-reservation-owner-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   const auto target_endpoint = listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   relay_node.peers().learn_endpoint(target.local_peer(), target_endpoint,
                                     capability_set{.bits = capabilities::direct_quic});
   (void)fcl::asio::blocking::run(runtime, source.async_reserve_relay(relay_node.local_peer()));

   try {
      (void)fcl::asio::blocking::run(
          runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                     node::open_options{
                                                         .allow_relay = true,
                                                         .relay_peer = relay_node.local_peer(),
                                                         .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                         .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                         .allow_hole_punch = false,
                                                     }));
      BOOST_FAIL("expected relay CONNECT rejection without target reservation");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::relay_rejected));
   }

   fcl::asio::blocking::run(runtime, target.async_stop());
   fcl::asio::blocking::run(runtime, source.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_stop_connect_passes_frames_after_target_reservation) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   const auto relay_identity = make_test_certificate_identity("relay-stop-connect-relay");
   const auto source_identity = make_test_certificate_identity("relay-stop-connect-source");
   const auto target_identity = make_test_certificate_identity("relay-stop-connect-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)fcl::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   auto stream = fcl::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .relay_peer = relay_node.local_peer(),
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'r', 'e', 'l', 'a', 'y'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(relay_node.metrics().relays_opened >= 1U);
   BOOST_TEST(source.metrics().path_relay_opens >= 1U);

   fcl::asio::blocking::run(runtime, target.async_stop());
   fcl::asio::blocking::run(runtime, source.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_transport_opens_arbitrary_registered_protocol) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   const auto protocol = protocol_id{.value = "/product/relay-arbitrary/1"};
   const auto relay_identity = make_test_certificate_identity("relay-arbitrary-protocol-relay");
   const auto source_identity = make_test_certificate_identity("relay-arbitrary-protocol-source");
   const auto target_identity = make_test_certificate_identity("relay-arbitrary-protocol-target");
   auto relay_node = node{
       runtime, options_for(relay_identity, capability_set{.bits = capabilities::direct_quic | capabilities::relay |
                                                                   capabilities::relay_reservation})};
   auto source = node{runtime, options_for(source_identity, capability_set{.bits = capabilities::direct_quic})};
   auto target = node{runtime, options_for(target_identity, capability_set{.bits = capabilities::direct_quic |
                                                                                   capabilities::relay_reservation})};
   register_echo(target, protocol);

   const auto relay_endpoint = listen(relay_node, runtime);
   (void)listen(target, runtime);
   source.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   target.peers().learn_endpoint(
       relay_node.local_peer(), relay_endpoint,
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)fcl::asio::blocking::run(runtime, target.async_reserve_relay(relay_node.local_peer()));

   auto stream = fcl::asio::blocking::run(
       runtime, source.async_open_protocol_stream(target.local_peer(), protocol,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .relay_peer = relay_node.local_peer(),
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{2'000},
                                                      .allow_hole_punch = false,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'p', 'r', 'o', 'd', 'u', 'c', 't'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(relay_node.metrics().relays_opened >= 1U);
   BOOST_TEST(source.metrics().path_relay_opens >= 1U);

   fcl::asio::blocking::run(runtime, target.async_stop());
   fcl::asio::blocking::run(runtime, source.async_stop());
   fcl::asio::blocking::run(runtime, relay_node.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_document_roundtrips_libp2p_fields) {
   const auto id = peer(74);
   auto doc = identify::document{
       .protocol_version = "/fcl/test/1",
       .agent_version = "fcl-test/1",
       .public_key = std::vector<std::uint8_t>{1, 2, 3},
       .listen_endpoints =
           std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string())},
       .observed_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/5001/quic-v1/p2p/" + id.to_string()),
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
       .signed_peer_record = std::vector<std::uint8_t>{9, 8, 7},
   };

   auto decoded = identify::decode(identify::encode(doc));

   BOOST_TEST(decoded.protocol_version == doc.protocol_version);
   BOOST_TEST(decoded.agent_version == doc.agent_version);
   BOOST_TEST(decoded.public_key == doc.public_key, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(decoded.listen_endpoints.size(), 1U);
   BOOST_TEST(decoded.listen_endpoints.front().to_string() == doc.listen_endpoints.front().to_string());
   BOOST_REQUIRE(decoded.observed_endpoint.has_value());
   BOOST_TEST(decoded.observed_endpoint->to_string() == doc.observed_endpoint->to_string());
   BOOST_REQUIRE_EQUAL(decoded.protocols.size(), 2U);
   BOOST_TEST(decoded.protocols.front().value == builtins::ping.value);
   BOOST_TEST(decoded.signed_peer_record == doc.signed_peer_record, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(p2p_direct_nodes_negotiate_protocol_and_echo_frames) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(2))};
   auto client = node{runtime, options_for(peer(1))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   const auto session = fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo));
   const auto payload = std::vector<std::uint8_t>{'p', '2', 'p'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().protocol_streams_opened >= 1U);
   BOOST_TEST(server.metrics().protocol_streams_accepted >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_uses_endpoint_peer_when_expected_peer_is_absent) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(make_test_certificate_identity("quic-endpoint-peer-server"));
   auto client_options = options_for(make_test_certificate_identity("quic-endpoint-peer-client"));
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_store_backend = peer_store::make_memory_backend();
   client_options.peer_store_backend = peer_store::make_memory_backend();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   auto server_endpoint = listen(server, runtime);
   server_endpoint.peer = server.local_peer();
   const auto session = fcl::asio::blocking::run(runtime, client.async_connect(server_endpoint));

   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_rejects_endpoint_peer_mismatch) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(make_test_certificate_identity("quic-endpoint-peer-mismatch-server"));
   auto client_options = options_for(make_test_certificate_identity("quic-endpoint-peer-mismatch-client"));
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_store_backend = peer_store::make_memory_backend();
   client_options.peer_store_backend = peer_store::make_memory_backend();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   auto server_endpoint = listen(server, runtime);
   server_endpoint.peer = peer(150);
   try {
      (void)fcl::asio::blocking::run(runtime, client.async_connect(server_endpoint));
      BOOST_FAIL("expected QUIC endpoint peer mismatch");
   } catch (const fcl::exceptions::base& error) {
      BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_explicit_expected_peer_matches_certificate_peer_id) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(make_test_certificate_identity("quic-expected-peer-server"));
   auto client_options = options_for(make_test_certificate_identity("quic-expected-peer-client"));
   server_options.allow_insecure_test_mode = false;
   client_options.allow_insecure_test_mode = false;
   server_options.peer_store_backend = peer_store::make_memory_backend();
   client_options.peer_store_backend = peer_store::make_memory_backend();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto server_endpoint = listen(server, runtime);
   const auto session = fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_rejects_missing_certificate_identity_without_expected_peer) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options = node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = legacy_cert_hash_peer_id(test_certificate()),
       .peer_store_backend = peer_store::make_memory_backend(),
   };
   auto client_options = options_for(make_test_certificate_identity("quic-missing-extension-client"));
   client_options.allow_insecure_test_mode = false;
   client_options.peer_store_backend = peer_store::make_memory_backend();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto server_endpoint = listen(server, runtime);
   try {
      (void)fcl::asio::blocking::run(runtime, client.async_connect(server_endpoint));
      BOOST_FAIL("expected missing QUIC certificate identity extension to fail");
   } catch (const fcl::exceptions::base& error) {
      BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_direct_quic_rejects_missing_certificate_identity_with_endpoint_peer) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto legacy_peer = legacy_cert_hash_peer_id(test_certificate());
   auto server_options = node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = legacy_peer,
       .peer_store_backend = peer_store::make_memory_backend(),
   };
   auto client_options = options_for(make_test_certificate_identity("quic-missing-extension-endpoint-client"));
   client_options.allow_insecure_test_mode = false;
   client_options.peer_store_backend = peer_store::make_memory_backend();
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};

   auto server_endpoint = listen(server, runtime);
   server_endpoint.peer = legacy_peer;
   try {
      (void)fcl::asio::blocking::run(runtime, client.async_connect(server_endpoint));
      BOOST_FAIL("expected missing QUIC certificate identity extension to fail");
   } catch (const fcl::exceptions::base& error) {
      BOOST_REQUIRE(fcl::p2p::exceptions::code_of(error).has_value());
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::peer_verification_failed));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_ping_protocol_uses_libp2p_payload_echo) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(72))};
   auto client = node{runtime, options_for(peer(73))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::ping));
   const auto payload = std::vector<std::uint8_t>(32, 0x42);
   fcl::asio::blocking::run(runtime, stream.async_write(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_ping_api_returns_rtt) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(82))};
   auto client = node{runtime, options_for(peer(83))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto rtt = fcl::asio::blocking::run(runtime, client.async_ping(server.local_peer()));
   BOOST_TEST(rtt.count() >= 0);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_protocol_advertises_supported_protocols) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(75))};
   auto client = node{runtime, options_for(peer(76))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto payload = fcl::asio::blocking::run(runtime, read_length_delimited(stream));
   const auto doc = identify::decode(payload);

   BOOST_TEST(doc.agent_version == "fcl/0.1.0");
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::ping; }));
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::identify; }));
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::echo; }));
   BOOST_TEST(!doc.listen_endpoints.empty());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_node_finds_peer_and_provider_over_negotiated_stream) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options = options_for(peer(118), capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   server_options.limits.dht.operating_mode = dht::mode::server;
   auto client_options = options_for(peer(119), capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::dht});

   const auto target = peer(120);
   const auto provider = peer(121);
   const auto target_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4120/quic-v1/p2p/" + target.to_string());
   const auto provider_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4121/quic-v1/p2p/" + provider.to_string());
   const auto key = make_dht_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 'n', 'o', 'd', 'e'});
   server.peers().upsert_routing_peer(
       dht::peer{
           .id = target,
           .endpoints = std::vector<endpoint>{target_endpoint},
           .connection = dht::connection_type::can_connect,
       },
       discovery::source::explicit_config, std::chrono::system_clock::now() + std::chrono::hours{1});
   server.peers().upsert_provider(peer_store::provider_record{
       .key = key,
       .provider =
           dht::peer{
               .id = provider,
               .endpoints = std::vector<endpoint>{provider_endpoint},
               .connection = dht::connection_type::connected,
           },
       .expires_at = std::chrono::system_clock::now() + std::chrono::hours{1},
   });

   const auto found_peer = fcl::asio::blocking::run(runtime, client.async_find_peer(target));
   BOOST_TEST(std::ranges::any_of(found_peer.closest_peers, [&](const dht::peer& value) {
      return value.id == target && !value.endpoints.empty() &&
             value.endpoints.front().to_string() == target_endpoint.to_string();
   }));

   const auto providers = fcl::asio::blocking::run(runtime, client.async_find_providers(key));
   BOOST_TEST(std::ranges::any_of(providers, [&](const dht::peer& value) {
      return value.id == provider && !value.endpoints.empty() &&
             value.endpoints.front().to_string() == provider_endpoint.to_string();
   }));
   BOOST_TEST(server.metrics().dht_queries >= 2U);
   BOOST_TEST(server.metrics().dht_responses >= 2U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_iterative_lookup_walks_many_peer_topology) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   const auto seed_identity = make_test_certificate_identity("dht-seed");
   const auto hop_identity = make_test_certificate_identity("dht-hop");
   const auto client_identity = make_test_certificate_identity("dht-client");
   auto seed_options =
       options_for(seed_identity, capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   seed_options.limits.dht.operating_mode = dht::mode::server;
   auto hop_options =
       options_for(hop_identity, capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   hop_options.limits.dht.operating_mode = dht::mode::server;
   auto client_options =
       options_for(client_identity, capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   client_options.limits.dht.alpha = 1;

   auto seed = node{runtime, std::move(seed_options)};
   auto hop = node{runtime, std::move(hop_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto seed_endpoint = listen(seed, runtime);
   const auto hop_endpoint = listen(hop, runtime);
   client.peers().learn_endpoint(seed.local_peer(), seed_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::dht});

   const auto target = peer(127);
   const auto target_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4127/quic-v1/p2p/" + target.to_string());
   seed.peers().upsert_routing_peer(
       dht::peer{
           .id = hop.local_peer(),
           .endpoints = std::vector<endpoint>{hop_endpoint},
           .connection = dht::connection_type::can_connect,
       },
       discovery::source::dht, std::chrono::system_clock::now() + std::chrono::hours{1});
   hop.peers().upsert_routing_peer(
       dht::peer{
           .id = target,
           .endpoints = std::vector<endpoint>{target_endpoint},
           .connection = dht::connection_type::can_connect,
       },
       discovery::source::dht, std::chrono::system_clock::now() + std::chrono::hours{1});

   const auto found = fcl::asio::blocking::run(runtime, client.async_find_peer(target));
   BOOST_TEST(found.complete);
   BOOST_TEST(std::ranges::any_of(found.closest_peers, [&](const dht::peer& value) {
      return value.id == target && !value.endpoints.empty() &&
             value.endpoints.front().to_string() == target_endpoint.to_string();
   }));
   BOOST_TEST(seed.metrics().dht_queries >= 1U);
   BOOST_TEST(hop.metrics().dht_queries >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, hop.async_stop());
   fcl::asio::blocking::run(runtime, seed.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_dht_iterative_provider_lookup_and_provide_reach_closest_peers) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   const auto seed_identity = make_test_certificate_identity("dht-provider-seed");
   const auto hop_identity = make_test_certificate_identity("dht-provider-hop");
   const auto client_identity = make_test_certificate_identity("dht-provider-client");
   auto seed_options =
       options_for(seed_identity, capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   seed_options.limits.dht.operating_mode = dht::mode::server;
   auto hop_options =
       options_for(hop_identity, capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   hop_options.limits.dht.operating_mode = dht::mode::server;
   auto client_options =
       options_for(client_identity, capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   client_options.limits.dht.alpha = 1;

   auto seed = node{runtime, std::move(seed_options)};
   auto hop = node{runtime, std::move(hop_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto seed_endpoint = listen(seed, runtime);
   const auto hop_endpoint = listen(hop, runtime);
   client.peers().learn_endpoint(seed.local_peer(), seed_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   seed.peers().upsert_routing_peer(
       dht::peer{
           .id = hop.local_peer(),
           .endpoints = std::vector<endpoint>{hop_endpoint},
           .connection = dht::connection_type::can_connect,
       },
       discovery::source::dht, std::chrono::system_clock::now() + std::chrono::hours{1});

   const auto key = make_dht_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 'm', 'u', 'l', 't',
                                                           'i', '-', 'h', 'o', 'p'});
   const auto provider = peer(131);
   const auto provider_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4131/quic-v1/p2p/" + provider.to_string());
   hop.peers().upsert_provider(peer_store::provider_record{
       .key = key,
       .provider =
           dht::peer{
               .id = provider,
               .endpoints = std::vector<endpoint>{provider_endpoint},
               .connection = dht::connection_type::connected,
           },
       .expires_at = std::chrono::system_clock::now() + std::chrono::hours{1},
   });
   const auto expired_provider = peer(132);
   const auto expired_provider_endpoint =
       parse_endpoint("/ip4/127.0.0.1/udp/4132/quic-v1/p2p/" + expired_provider.to_string());
   hop.peers().upsert_provider(peer_store::provider_record{
       .key = key,
       .provider =
           dht::peer{
               .id = expired_provider,
               .endpoints = std::vector<endpoint>{expired_provider_endpoint},
               .connection = dht::connection_type::connected,
           },
       .expires_at = std::chrono::system_clock::now() - std::chrono::seconds{1},
   });

   const auto providers = fcl::asio::blocking::run(runtime, client.async_find_providers(key));
   BOOST_TEST(std::ranges::any_of(providers, [&](const dht::peer& value) {
      return value.id == provider && !value.endpoints.empty() &&
             value.endpoints.front().to_string() == provider_endpoint.to_string();
   }));
   BOOST_TEST(!std::ranges::any_of(providers, [&](const dht::peer& value) {
      return value.id == expired_provider;
   }));

   const auto publish_key =
       make_dht_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'd', 'h', 't', '-', 'r', 'e', 'p', 'u', 'b'});
   fcl::asio::blocking::run(runtime, client.async_provide(publish_key));
   auto provider_stored = false;
   for (auto attempt = 0U; attempt < 20U && !provider_stored; ++attempt) {
      const auto stored = hop.peers().find_providers(publish_key);
      provider_stored = std::ranges::any_of(stored, [&](const peer_store::provider_record& value) {
         return value.provider.id == client.local_peer();
      });
      if (!provider_stored) {
         wait_on_runtime(runtime, std::chrono::milliseconds{50}, "DHT provide propagation");
      }
   }
   BOOST_TEST(provider_stored);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, hop.async_stop());
   fcl::asio::blocking::run(runtime, seed.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_node_registers_and_discovers_over_negotiated_stream) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options =
       options_for(peer(122), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   server_options.limits.rendezvous.require_signed_peer_record = false;
   auto client_options =
       options_for(peer(123), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   client_options.limits.rendezvous.require_signed_peer_record = false;
   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   const auto response = fcl::asio::blocking::run(
       runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                          .namespace_name = "fcl.discovery",
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
   BOOST_TEST(static_cast<int>(response.status_value) == static_cast<int>(rendezvous::status::ok));
   BOOST_TEST(response.ttl == std::chrono::seconds{7'200});

   const auto discovered = fcl::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "fcl.discovery",
                                                                          .limit = 10,
                                                                      }));
   BOOST_TEST(static_cast<int>(discovered.status_value) == static_cast<int>(rendezvous::status::ok));
   BOOST_REQUIRE_EQUAL(discovered.registrations.size(), 1U);
   BOOST_TEST(discovered.registrations.front().namespace_name == "fcl.discovery");
   BOOST_TEST(discovered.registrations.front().peer.to_string().empty());
   BOOST_TEST(discovered.registrations.front().signed_peer_record.empty());
   BOOST_TEST(rendezvous::codec::read_cookie(discovered.cookie) >= 1U);
   BOOST_TEST(server.metrics().rendezvous_registrations >= 1U);
   BOOST_TEST(server.metrics().rendezvous_discovers >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_rendezvous_refresh_replaces_registration_and_cookie_discovers_new_records) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_options =
       options_for(peer(132), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   server_options.limits.rendezvous.operating_role = rendezvous::role::server;
   auto identity = make_test_certificate_identity("rendezvous-refresh-client");
   auto client_options =
       options_for(identity, capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   const auto first_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4133/quic-v1/p2p/" + client.local_peer().to_string());
   const auto second_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4134/quic-v1/p2p/" + client.local_peer().to_string());
   const auto first_record = make_signed_rendezvous_peer_record(identity, std::vector<endpoint>{first_endpoint}, 1);
   const auto second_record = make_signed_rendezvous_peer_record(identity, std::vector<endpoint>{second_endpoint}, 2);

   auto first = fcl::asio::blocking::run(
       runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                          .namespace_name = "fcl.discovery",
                                                                          .signed_peer_record = first_record,
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
   BOOST_TEST(static_cast<int>(first.status_value) == static_cast<int>(rendezvous::status::ok));

   const auto after_first = fcl::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "fcl.discovery",
                                                                          .limit = 10,
                                                                      }));
   BOOST_REQUIRE_EQUAL(after_first.registrations.size(), 1U);
   const auto cookie = after_first.cookie;

   auto second = fcl::asio::blocking::run(
       runtime, client.async_rendezvous_register(server.local_peer(), rendezvous::register_request{
                                                                          .namespace_name = "fcl.discovery",
                                                                          .signed_peer_record = second_record,
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
   BOOST_TEST(static_cast<int>(second.status_value) == static_cast<int>(rendezvous::status::ok));

   const auto after_cookie = fcl::asio::blocking::run(
       runtime, client.async_rendezvous_discover(server.local_peer(), rendezvous::discover_request{
                                                                          .namespace_name = "fcl.discovery",
                                                                          .limit = 10,
                                                                          .cookie = cookie,
                                                                      }));
   BOOST_REQUIRE_EQUAL(after_cookie.registrations.size(), 1U);
   BOOST_TEST(after_cookie.registrations.front().peer.to_string() == client.local_peer().to_string());
   BOOST_REQUIRE_EQUAL(after_cookie.registrations.front().endpoints.size(), 1U);
   BOOST_TEST(after_cookie.registrations.front().endpoints.front().to_string() == second_endpoint.to_string());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_discovery_refresh_learns_dht_and_rendezvous_relay_candidates_for_autorelay) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto relay_identity = make_test_certificate_identity("discovery-refresh-relay");
   auto dht_options = options_for(peer(133), capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   dht_options.limits.dht.operating_mode = dht::mode::server;
   auto rendezvous_options =
       options_for(peer(134), capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   rendezvous_options.limits.rendezvous.operating_role = rendezvous::role::server;
   auto relay_options = options_for(relay_identity, capability_set{.bits = capabilities::direct_quic |
                                                                            capabilities::relay |
                                                                            capabilities::relay_reservation});
   auto client_options = options_for(peer(135), capability_set{.bits = capabilities::direct_quic |
                                                               capabilities::dht | capabilities::rendezvous |
                                                               capabilities::relay_reservation});
   client_options.relay_policy.auto_discovery_enabled = true;
   client_options.relay_policy.target_reservations = 1;
   client_options.relay_policy.max_candidates_per_refresh = 4;
   client_options.limits.discovery.max_results = 4;

   auto dht_server = node{runtime, std::move(dht_options)};
   auto rendezvous_server = node{runtime, std::move(rendezvous_options)};
   auto relay = node{runtime, std::move(relay_options)};
   auto client = node{runtime, std::move(client_options)};

   const auto dht_endpoint = listen(dht_server, runtime);
   const auto rendezvous_endpoint = listen(rendezvous_server, runtime);
   const auto relay_endpoint = listen(relay, runtime);

   dht_server.peers().upsert_routing_peer(
       dht::peer{
           .id = relay.local_peer(),
           .endpoints = std::vector<endpoint>{relay_endpoint},
           .connection = dht::connection_type::can_connect,
       },
       discovery::source::dht, std::chrono::system_clock::now() + std::chrono::hours{1});
   relay.peers().learn_endpoint(rendezvous_server.local_peer(), rendezvous_endpoint,
                                capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});
   const auto relay_record =
       make_signed_rendezvous_peer_record(relay_identity, std::vector<endpoint>{relay_endpoint}, 1);
   auto registered = fcl::asio::blocking::run(
       runtime, relay.async_rendezvous_register(rendezvous_server.local_peer(), rendezvous::register_request{
                                                                                  .namespace_name = "fcl.discovery",
                                                                                  .signed_peer_record = relay_record,
                                                                                  .ttl = std::chrono::seconds{7'200},
                                                                              }));
   BOOST_TEST(static_cast<int>(registered.status_value) == static_cast<int>(rendezvous::status::ok));

   client.peers().learn_endpoint(dht_server.local_peer(), dht_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::dht});
   client.peers().learn_endpoint(rendezvous_server.local_peer(), rendezvous_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::rendezvous});

   const auto discovered = fcl::asio::blocking::run(runtime, client.async_refresh_discovery());
   BOOST_TEST(std::ranges::any_of(discovered, [&](const discovery::result& value) {
      return value.peer == relay.local_peer();
   }));
   const auto learned = client.peers().find(relay.local_peer());
   BOOST_REQUIRE(learned.has_value());
   BOOST_TEST(learned->capabilities.has(capabilities::relay));
   BOOST_TEST(learned->capabilities.has(capabilities::relay_reservation));

   const auto reservations = fcl::asio::blocking::run(runtime, client.async_refresh_relay_candidates());
   BOOST_REQUIRE_EQUAL(reservations.size(), 1U);
   BOOST_TEST(reservations.front().relay_peer.to_string() == relay.local_peer().to_string());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay.async_stop());
   fcl::asio::blocking::run(runtime, rendezvous_server.async_stop());
   fcl::asio::blocking::run(runtime, dht_server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_nodes_deliver_signed_publish_over_negotiated_stream) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   subscriber_options.explicit_peer_id = peer(150);
   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto received = std::make_shared<std::promise<std::vector<std::uint8_t>>>();
   auto future = received->get_future();
   fcl::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    pubsub::topic{.value = "fcl.pubsub"},
                    [received](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       received->set_value(event.value.data);
                       co_return pubsub::validation_result::accept;
                    }));

   const auto published = fcl::asio::blocking::run(
       runtime, publisher.async_publish(pubsub::topic{.value = "fcl.pubsub"},
                                        std::vector<std::uint8_t>{'p', 'u', 'b', 's', 'u', 'b'}));
   BOOST_TEST(!published.signature.empty());

   if (future.wait_for(std::chrono::milliseconds{5'000}) != std::future_status::ready) {
      const auto metrics = subscriber.metrics();
      BOOST_FAIL("pubsub delivery did not finish; received="
                 << metrics.pubsub_messages_received << " delivered=" << metrics.pubsub_messages_delivered
                 << " invalid=" << metrics.pubsub_invalid_messages << " duplicates=" << metrics.pubsub_duplicates
                 << " rejected=" << metrics.protocol_rejections);
   }
   BOOST_TEST(future.get() == std::vector<std::uint8_t>({'p', 'u', 'b', 's', 'u', 'b'}),
              boost::test_tools::per_element());
   BOOST_TEST(publisher.pubsub_snapshot().messages_published >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered >= 1U);

   fcl::asio::blocking::run(runtime, publisher.async_stop());
   fcl::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_forwards_between_subscribed_peers) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 6}};
   auto publisher_options = pubsub_options_for();
   auto hub_options = pubsub_options_for();
   auto subscriber_options = pubsub_options_for();
   hub_options.explicit_peer_id = peer(151);
   subscriber_options.explicit_peer_id = peer(152);

   auto publisher = node{runtime, std::move(publisher_options)};
   auto hub = node{runtime, std::move(hub_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto hub_endpoint = listen(hub, runtime);
   const auto subscriber_endpoint = listen(subscriber, runtime);

   publisher.peers().learn_endpoint(hub.local_peer(), hub_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   subscriber.peers().learn_endpoint(hub.local_peer(), hub_endpoint,
                                     capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   hub.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto received = std::make_shared<std::promise<std::vector<std::uint8_t>>>();
   auto future = received->get_future();
   fcl::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    pubsub::topic{.value = "fcl.mesh"},
                    [received](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       received->set_value(event.value.data);
                       co_return pubsub::validation_result::accept;
                    }));

   fcl::asio::blocking::run(runtime, publisher.async_publish(pubsub::topic{.value = "fcl.mesh"},
                                                             std::vector<std::uint8_t>{'m', 'e', 's', 'h'}));

   if (future.wait_for(std::chrono::milliseconds{5'000}) != std::future_status::ready) {
      const auto hub_metrics = hub.metrics();
      const auto subscriber_metrics = subscriber.metrics();
      BOOST_FAIL("pubsub forwarding did not finish; hub_received="
                 << hub_metrics.pubsub_messages_received << " hub_delivered=" << hub_metrics.pubsub_messages_delivered
                 << " subscriber_received=" << subscriber_metrics.pubsub_messages_received
                 << " subscriber_delivered=" << subscriber_metrics.pubsub_messages_delivered
                 << " subscriber_invalid=" << subscriber_metrics.pubsub_invalid_messages);
   }
   BOOST_TEST(future.get() == std::vector<std::uint8_t>({'m', 'e', 's', 'h'}), boost::test_tools::per_element());
   BOOST_TEST(hub.pubsub_snapshot().mesh_edges >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered >= 1U);

   fcl::asio::blocking::run(runtime, publisher.async_stop());
   fcl::asio::blocking::run(runtime, hub.async_stop());
   fcl::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_control_spam_is_penalized_without_stopping_node) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.pubsub.limits.max_ihave_per_peer = 1;
   server_options.limits.pubsub.limits.max_iwant_per_peer = 1;
   server_options.limits.pubsub.limits.max_graft_per_peer = 1;
   auto client_options = pubsub_options_for();
   client_options.explicit_peer_id = peer(160);

   auto server = node{runtime, std::move(server_options)};
   auto client = node{runtime, std::move(client_options)};
   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   fcl::asio::blocking::run(
       runtime, server.async_subscribe(pubsub::topic{.value = "fcl.spam"},
                                       [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
                                          co_return pubsub::validation_result::accept;
                                       }));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
   const auto id = std::vector<std::uint8_t>{'i', 'd'};
   auto spam =
       pubsub::rpc{
           .control_value =
               pubsub::control{
                   .have =
                       std::vector<pubsub::control::ihave>{
                           pubsub::control::ihave{.subject = pubsub::topic{.value = "fcl.spam"},
                                                  .message_ids = std::vector<std::vector<std::uint8_t>>{id}},
                           pubsub::control::ihave{.subject = pubsub::topic{.value = "fcl.spam"},
                                                  .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                   .want =
                       std::vector<pubsub::control::iwant>{
                           pubsub::control::iwant{.message_ids = std::vector<std::vector<std::uint8_t>>{id}},
                           pubsub::control::iwant{.message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
                   .grafts =
                       std::vector<pubsub::control::graft>{
                           pubsub::control::graft{.subject = pubsub::topic{.value = "fcl.spam"}},
                           pubsub::control::graft{.subject = pubsub::topic{.value = "fcl.spam"}}},
               },
       };
   fcl::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(spam)));
   wait_on_runtime(runtime, std::chrono::milliseconds{250}, "gossipsub spam accounting");

   BOOST_TEST(server.metrics().pubsub_invalid_messages >= 1U);
   BOOST_TEST(server.metrics().protocol_rejections >= 1U);
   BOOST_TEST(!server.metrics().stopped);

   fcl::asio::blocking::run(runtime, stream.async_close());
   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_abusive_peer_crossing_malformed_threshold_closes_only_offender_session) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto server_options = pubsub_options_for();
   server_options.limits.resources.max_malformed_messages_per_peer = 1;
   server_options.limits.max_sessions = 2;
   server_options.limits.max_inbound_sessions = 2;
   server_options.limits.session_low_watermark = 2;
   server_options.limits.pubsub.limits.max_ihave_per_peer = 1;
   auto bad_options = pubsub_options_for();
   bad_options.explicit_peer_id = peer(248);
   auto good_options = pubsub_options_for();
   good_options.explicit_peer_id = peer(249);

   auto server = node{runtime, std::move(server_options)};
   auto bad = node{runtime, std::move(bad_options)};
   auto good = node{runtime, std::move(good_options)};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   bad.peers().learn_endpoint(server.local_peer(), server_endpoint,
                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   good.peers().learn_endpoint(server.local_peer(), server_endpoint,
                               capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   (void)fcl::asio::blocking::run(
       runtime, bad.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   (void)fcl::asio::blocking::run(
       runtime, good.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto id = std::vector<std::uint8_t>{'i', 'd'};
   auto spam = pubsub::rpc{
       .control_value =
           pubsub::control{
               .have =
                   std::vector<pubsub::control::ihave>{
                       pubsub::control::ihave{.subject = pubsub::topic{.value = "fcl.abuse"},
                                              .message_ids = std::vector<std::vector<std::uint8_t>>{id}},
                       pubsub::control::ihave{.subject = pubsub::topic{.value = "fcl.abuse"},
                                              .message_ids = std::vector<std::vector<std::uint8_t>>{id}}},
           },
   };
   for (auto index = 0; index != 2; ++index) {
      auto stream =
          fcl::asio::blocking::run(runtime, bad.async_open_protocol_stream(server.local_peer(), builtins::meshsub_v11));
      fcl::asio::blocking::run(runtime, stream.async_write(pubsub::codec::encode(spam)));
      wait_on_runtime(runtime, std::chrono::milliseconds{150}, "gossipsub abuse accounting");
   }

   BOOST_TEST(server.metrics().pubsub_invalid_messages >= 2U);
   BOOST_TEST(server.metrics().sessions_closed >= 1U);
   BOOST_TEST(server.metrics().connection_rejections >= 1U);

   auto stream =
       fcl::asio::blocking::run(runtime, good.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                                         node::open_options{.allow_relay = false}));
   const auto payload = std::vector<std::uint8_t>{'g', 'o', 'o', 'd'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, good.async_stop());
   fcl::asio::blocking::run(runtime, bad.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_outbound_byte_limit_rejects_publish_without_stopping_node) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto publisher_options = pubsub_options_for();
   publisher_options.limits.pubsub.limits.max_outbound_queue_bytes = 8;
   auto subscriber_options = pubsub_options_for();
   subscriber_options.explicit_peer_id = peer(161);

   auto publisher = node{runtime, std::move(publisher_options)};
   auto subscriber = node{runtime, std::move(subscriber_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                    capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   fcl::asio::blocking::run(
       runtime, subscriber.async_subscribe(pubsub::topic{.value = "fcl.limit"},
                                           [](pubsub::event) -> boost::asio::awaitable<pubsub::validation_result> {
                                              co_return pubsub::validation_result::accept;
                                           }));

   BOOST_CHECK_THROW(
       (void)fcl::asio::blocking::run(runtime, publisher.async_publish(pubsub::topic{.value = "fcl.limit"},
                                                                       std::vector<std::uint8_t>{'o', 'v', 'e', 'r'})),
       fcl::exceptions::base);
   BOOST_TEST(publisher.metrics().backpressure_rejections >= 1U);
   BOOST_TEST(!publisher.metrics().stopped);

   fcl::asio::blocking::run(runtime, publisher.async_stop());
   fcl::asio::blocking::run(runtime, subscriber.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_validation_queue_limit_drops_excess_and_shutdown_is_clean) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 6}};
   auto subscriber_options = pubsub_options_for(make_test_certificate_identity("pubsub-validation-subscriber"));
   subscriber_options.limits.pubsub.limits.max_validation_queue = 1;
   subscriber_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   auto publisher_a_options = pubsub_options_for(make_test_certificate_identity("pubsub-validation-a"));
   publisher_a_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
   auto publisher_b_options = pubsub_options_for(make_test_certificate_identity("pubsub-validation-b"));
   publisher_b_options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;

   auto subscriber = node{runtime, std::move(subscriber_options)};
   auto publisher_a = node{runtime, std::move(publisher_a_options)};
   auto publisher_b = node{runtime, std::move(publisher_b_options)};
   const auto subscriber_endpoint = listen(subscriber, runtime);
   publisher_a.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                      capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
   publisher_b.peers().learn_endpoint(subscriber.local_peer(), subscriber_endpoint,
                                      capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});

   auto entered = std::make_shared<std::atomic_uint64_t>(0);
   fcl::asio::blocking::run(
       runtime, subscriber.async_subscribe(
                    pubsub::topic{.value = "fcl.validation"},
                    [entered](pubsub::event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                       entered->fetch_add(1, std::memory_order_relaxed);
                       auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
                       timer.expires_after(std::chrono::milliseconds{500});
                       boost::system::error_code ec;
                       co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
                       co_return pubsub::validation_result::accept;
                    }));

   auto publish_a = boost::asio::co_spawn(
       runtime.context(),
       [&publisher_a]() -> boost::asio::awaitable<void> {
          (void)co_await publisher_a.async_publish(pubsub::topic{.value = "fcl.validation"},
                                                   std::vector<std::uint8_t>{'a'},
                                                   pubsub::publish_options{.sign = false});
       },
       boost::asio::use_future);
   auto publish_b = boost::asio::co_spawn(
       runtime.context(),
       [&publisher_b]() -> boost::asio::awaitable<void> {
          (void)co_await publisher_b.async_publish(pubsub::topic{.value = "fcl.validation"},
                                                   std::vector<std::uint8_t>{'b'},
                                                   pubsub::publish_options{.sign = false});
       },
       boost::asio::use_future);
   wait_for_server(publish_a, std::chrono::seconds{5}, "first validation publish");
   wait_for_server(publish_b, std::chrono::seconds{5}, "second validation publish");
   wait_on_runtime(runtime, std::chrono::milliseconds{750}, "validation queue drain");

   BOOST_TEST(entered->load(std::memory_order_relaxed) == 1U);
   BOOST_TEST(subscriber.metrics().pubsub_invalid_messages >= 1U);
   BOOST_TEST(subscriber.pubsub_snapshot().messages_delivered == 1U);

   fcl::asio::blocking::run(runtime, subscriber.async_stop());
   BOOST_TEST(subscriber.metrics().stopped);
   fcl::asio::blocking::run(runtime, publisher_a.async_stop());
   fcl::asio::blocking::run(runtime, publisher_b.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_gossipsub_ten_node_mesh_delivers_multiple_publishes_once) {
   constexpr auto node_count = std::size_t{10};
   constexpr auto publish_count = std::size_t{3};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 12}};
   auto identities = std::vector<test_certificate_identity>{};
   auto nodes = std::vector<std::unique_ptr<node>>{};
   auto endpoints = std::vector<endpoint>{};
   identities.reserve(node_count);
   nodes.reserve(node_count);
   endpoints.reserve(node_count);
   for (auto index = std::size_t{}; index < node_count; ++index) {
      identities.push_back(make_test_certificate_identity("pubsub-mesh-" + std::to_string(index)));
      auto options = pubsub_options_for(identities.back());
      options.limits.pubsub.signatures = pubsub::signature_policy::lax_no_sign;
      nodes.push_back(std::make_unique<node>(runtime, std::move(options)));
      endpoints.push_back(listen(*nodes.back(), runtime));
   }
   for (auto index = std::size_t{}; index < node_count; ++index) {
      for (auto peer_index = std::size_t{}; peer_index < node_count; ++peer_index) {
         if (index == peer_index) {
            continue;
         }
         nodes[index]->peers().learn_endpoint(nodes[peer_index]->local_peer(), endpoints[peer_index],
                                              capability_set{.bits = capabilities::direct_quic | capabilities::pubsub});
      }
   }

   struct delivery_state {
      std::mutex mutex;
      std::condition_variable cv;
      std::map<std::string, std::set<std::size_t>> delivered;
      std::uint64_t duplicates = 0;
   };
   auto state = std::make_shared<delivery_state>();
   for (auto index = std::size_t{}; index < node_count; ++index) {
      fcl::asio::blocking::run(
          runtime,
          nodes[index]->async_subscribe(
              pubsub::topic{.value = "fcl.mesh.stress"},
              [state, index](pubsub::event event) mutable -> boost::asio::awaitable<pubsub::validation_result> {
                 const auto payload = std::string{event.value.data.begin(), event.value.data.end()};
                 {
                    auto lock = std::unique_lock{state->mutex};
                    if (!state->delivered[payload].insert(index).second) {
                       ++state->duplicates;
                    }
                 }
                 state->cv.notify_all();
                 co_return pubsub::validation_result::accept;
              }));
   }

   for (auto index = std::size_t{}; index < publish_count; ++index) {
      const auto payload = std::string{"stress-" + std::to_string(index)};
      fcl::asio::blocking::run(runtime,
                               nodes[index]->async_publish(pubsub::topic{.value = "fcl.mesh.stress"},
                                                           std::vector<std::uint8_t>{payload.begin(), payload.end()},
                                                           pubsub::publish_options{.sign = false}));
   }

   {
      auto lock = std::unique_lock{state->mutex};
      const auto completed = state->cv.wait_for(lock, std::chrono::seconds{15}, [&] {
         for (auto index = std::size_t{}; index < publish_count; ++index) {
            const auto payload = std::string{"stress-" + std::to_string(index)};
            if (state->delivered[payload].size() < node_count - 1) {
               return false;
            }
         }
         return true;
      });
      BOOST_REQUIRE(completed);
      BOOST_TEST(state->duplicates == 0U);
   }

   for (auto index = std::size_t{}; index < publish_count; ++index) {
      const auto payload = std::string{"stress-" + std::to_string(index)};
      BOOST_TEST(!state->delivered[payload].contains(index));
   }
   for (auto& value : nodes) {
      fcl::asio::blocking::run(runtime, value->async_stop());
   }
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_updates_peer_store) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-push-update-server");
   const auto client_identity = make_test_certificate_identity("identify-push-update-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto pushed = identify::document{
       .protocol_version = "/fcl/push-test/1",
       .agent_version = "fcl-push-test/1",
       .listen_endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4101/quic-v1/p2p/" +
                                                                client.local_peer().to_string())},
       .protocols = std::vector<protocol_id>{builtins::ping},
   };
   fcl::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   fcl::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push propagation");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/fcl/push-test/1");
   BOOST_TEST(
       std::ranges::any_of(found->protocols, [](const protocol_id& protocol) { return protocol == builtins::ping; }));
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.peer->to_bytes() == client.local_peer().to_bytes(),
              boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_rejects_mismatched_endpoint_peer_suffix) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   const auto server_identity = make_test_certificate_identity("identify-push-mismatch-server");
   const auto client_identity = make_test_certificate_identity("identify-push-mismatch-client");
   auto server = node{runtime, options_for(server_identity)};
   auto client = node{runtime, options_for(client_identity)};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto bad_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4107/quic-v1/p2p/" + peer(215).to_string());
   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto pushed = identify::document{
       .protocol_version = "/fcl/push-bad-peer/1",
       .agent_version = "fcl-push-bad-peer/1",
       .listen_endpoints = std::vector<endpoint>{bad_endpoint},
       .protocols = std::vector<protocol_id>{builtins::ping},
   };
   fcl::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
   fcl::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push mismatch propagation");

   const auto found = server.peers().find(client.local_peer());
   BOOST_REQUIRE(found);
   BOOST_TEST(found->protocol_version == "/fcl/push-bad-peer/1");
   BOOST_TEST(std::ranges::none_of(found->endpoints, [&](const peer_store::endpoint_record& record) {
      return record.endpoint.to_string() == bad_endpoint.to_string();
   }));

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

#if FCL_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(p2p_identify_push_persists_rocksdb_peer_record) {
   auto temp = temp_store_dir{"identify-push-rocksdb"};
   const auto server_identity = make_test_certificate_identity("identify-push-rocksdb-server");
   const auto client_identity = make_test_certificate_identity("identify-push-rocksdb-client");

   {
      auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
      auto server_options = options_for(server_identity);
      server_options.peer_store_path = temp.path();
      auto server = node{runtime, std::move(server_options)};
      auto client = node{runtime, options_for(client_identity)};

      const auto server_endpoint = listen(server, runtime);
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

      auto stream = fcl::asio::blocking::run(
          runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
      auto pushed = identify::document{
          .protocol_version = "/fcl/push-persist/1",
          .agent_version = "fcl-push-persist/1",
          .public_key = std::vector<std::uint8_t>{9, 8, 7},
          .listen_endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4201/quic-v1/p2p/" +
                                                                   client.local_peer().to_string())},
          .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
          .signed_peer_record = std::vector<std::uint8_t>{6, 5, 4},
      };
      const auto decoded = identify::decode(identify::encode(pushed));
      BOOST_REQUIRE_EQUAL(decoded.listen_endpoints.size(), 1U);
      BOOST_REQUIRE(decoded.listen_endpoints.front().peer.has_value());
      BOOST_TEST(decoded.listen_endpoints.front().peer->to_bytes() == client.local_peer().to_bytes(),
                 boost::test_tools::per_element());
      fcl::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(identify::encode(pushed))));
      fcl::asio::blocking::run(runtime, stream.async_close());
      wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push persistence");

      fcl::asio::blocking::run(runtime, client.async_stop());
      fcl::asio::blocking::run(runtime, server.async_stop());
   }

   auto reopened = peer_store{peer_store::options{
       .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
   }};
   const auto snapshot = reopened.snapshot();
   const auto found = std::ranges::find_if(
       snapshot, [](const peer_store::record& value) { return value.protocol_version == "/fcl/push-persist/1"; });
   BOOST_REQUIRE(found != snapshot.end());
   BOOST_TEST(found->peer.to_bytes() == client_identity.peer.to_bytes(), boost::test_tools::per_element());
   BOOST_TEST(found->agent_version == "fcl-push-persist/1");
   BOOST_TEST(found->public_key == std::vector<std::uint8_t>({9, 8, 7}), boost::test_tools::per_element());
   BOOST_TEST(found->signed_peer_record == std::vector<std::uint8_t>({6, 5, 4}), boost::test_tools::per_element());
   BOOST_TEST(
       std::ranges::any_of(found->protocols, [](const protocol_id& value) { return value == builtins::identify; }));
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.transport.port == 4201);
}
#endif

BOOST_AUTO_TEST_CASE(p2p_unsupported_protocol_rejection_keeps_session_usable) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(79))};
   auto client = node{runtime, options_for(peer(80))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_open_protocol_stream(server.local_peer(), protocol_id{.value = "/product/missing/1"}));
      BOOST_FAIL("expected unsupported protocol rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::unsupported_protocol));
   }

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::ping));
   const auto payload = std::vector<std::uint8_t>(32, 0x24);
   fcl::asio::blocking::run(runtime, stream.async_write(payload));
   BOOST_TEST(fcl::asio::blocking::run(runtime, stream.async_read()) == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_path_manager_tries_next_direct_endpoint_after_attempt_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(64))};
   auto client = node{runtime, options_for(peer(65))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), make_quic_endpoint(9),
                                 capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = false,
                                                      .timeout = std::chrono::milliseconds{2'000},
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .max_direct_endpoints = 2,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'d', 'i', 'r', 'e', 'c', 't'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_attempts >= 2U);
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);
   auto record = client.peers().find(server.local_peer());
   BOOST_REQUIRE(record.has_value());
   auto failed = std::ranges::find_if(record->endpoints, [&](const peer_store::endpoint_record& current) {
      return current.endpoint.to_string() == make_quic_endpoint(9).to_string();
   });
   auto succeeded = std::ranges::find_if(record->endpoints, [&](const peer_store::endpoint_record& current) {
      return current.endpoint.to_string() == server_endpoint.to_string();
   });
   BOOST_REQUIRE(failed != record->endpoints.end());
   BOOST_REQUIRE(succeeded != record->endpoints.end());
   BOOST_TEST(failed->failures >= 1U);
   BOOST_TEST(failed->backoff_until > std::chrono::system_clock::now());
   BOOST_TEST(succeeded->successes >= 1U);
   BOOST_TEST(succeeded->backoff_until == std::chrono::system_clock::time_point{});

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_path_manager_tries_next_tcp_endpoint_after_upgrade_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(205))};
   auto client = node{runtime, options_for(peer(206))};
   register_echo(server);

   const auto stalled_endpoint = start_stalling_tcp_peer(runtime);
   const auto server_endpoint = listen_tcp(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), stalled_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = false,
                                                      .timeout = std::chrono::milliseconds{2'000},
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .max_direct_endpoints = 2,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'t', 'c', 'p', '-', 'd', 'e', 'a', 'd'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_attempts >= 2U);
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_successful_connect_deadline_does_not_poison_session) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(41))};
   auto client = node{runtime, options_for(peer(42))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime,
       client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer(),
                                                                   .timeout = std::chrono::milliseconds{500}}));
   wait_on_runtime(runtime, std::chrono::milliseconds{700}, "post-connect deadline grace");

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{.timeout = std::chrono::milliseconds{1'000}}));
   const auto payload = std::vector<std::uint8_t>{'d', 'e', 'a', 'd', 'l', 'i', 'n', 'e'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_duplicate_protocol_handler_is_rejected) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(3))};
   register_echo(value);

   try {
      register_echo(value);
      BOOST_FAIL("expected duplicate protocol handler rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::duplicate_protocol));
   }
}

BOOST_AUTO_TEST_CASE(p2p_product_message_packs_typed_payload_as_data) {
   const auto protocol = protocol_id{.value = "/product/chunk-announce/1"};
   const auto value = product_announce{.ref = "chunk-1"};

   const auto message = fcl::p2p::message{protocol, value};

   BOOST_TEST(message.protocol().value == protocol.value);
   BOOST_TEST(message.codec().value == "fcl.raw");
   BOOST_TEST(message.as<product_announce>().ref == value.ref);
   BOOST_TEST(!message.data().empty());
}

BOOST_AUTO_TEST_CASE(p2p_connect_rejects_non_positive_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(32))};

   try {
      (void)fcl::asio::blocking::run(
          runtime,
          client.async_connect(make_quic_endpoint(9), node::connect_options{.expected_peer = peer(33),
                                                                            .timeout = std::chrono::milliseconds{0}}));
      BOOST_FAIL("expected invalid connect timeout");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_open_protocol_rejects_non_positive_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(39))};

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_open_protocol_stream(peer(40), builtins::echo,
                                                     node::open_options{.timeout = std::chrono::milliseconds{0}}));
      BOOST_FAIL("expected invalid protocol open timeout");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_expires_stale_reachability_observation) {
   auto store = peer_store{};
   store.upsert(peer_store::record{
       .peer = peer(70),
       .capabilities = capability_set{.bits = capabilities::direct_quic},
       .reachability = reachability::state::publicly_reachable,
       .observed_endpoint = make_quic_endpoint(12345),
       .reachability_expires_at = std::chrono::system_clock::now() - std::chrono::seconds{1},
   });

   const auto record = store.find(peer(70));
   BOOST_REQUIRE(record.has_value());
   BOOST_TEST(static_cast<int>(record->reachability) == static_cast<int>(reachability::state::unknown));
   BOOST_TEST(!record->observed_endpoint.has_value());

   const auto snapshot = store.snapshot();
   BOOST_REQUIRE_EQUAL(snapshot.size(), 1U);
   BOOST_TEST(static_cast<int>(snapshot.front().reachability) == static_cast<int>(reachability::state::unknown));
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_uses_injected_backend) {
   auto backend = std::make_shared<counting_peer_store_backend>();
   auto store = peer_store{peer_store::options{.backend = backend}};
   const auto id = peer(81);

   store.upsert(peer_store::record{
       .peer = id,
       .protocol_version = "/fcl/test/1",
       .agent_version = "fcl-test/1",
       .protocols = std::vector<protocol_id>{builtins::ping},
   });
   store.learn_endpoint(id, make_quic_endpoint(4001), capability_set{.bits = capabilities::direct_quic});

   BOOST_TEST(backend->upsert_count == 1U);
   BOOST_TEST(backend->learn_endpoint_count == 1U);
   const auto found = store.find(id);
   BOOST_REQUIRE(found.has_value());
   BOOST_TEST(found->protocol_version == "/fcl/test/1");
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.transport.port == 4001);
}

#if FCL_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(p2p_peer_store_rocksdb_survives_reopen) {
   auto temp = temp_store_dir{"rocksdb-reopen"};
   const auto id = peer(82);
   const auto relay_id = peer(83);
   const auto endpoint = make_quic_endpoint(4101);
   const auto observed = make_quic_endpoint(4102);
   const auto relay_endpoint = make_quic_endpoint(4103);
   const auto reservation_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{15};

   {
      auto store = peer_store{peer_store::options{
          .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
      }};
      store.upsert(peer_store::record{
          .peer = id,
          .capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange},
          .protocol_version = "/fcl/reopen/1",
          .agent_version = "fcl-reopen/1",
          .public_key = std::vector<std::uint8_t>{1, 2, 3, 4},
          .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
          .signed_peer_record = std::vector<std::uint8_t>{5, 6, 7},
          .endpoints = std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{
              .endpoint = endpoint,
              .kind = path::kind::direct,
              .successes = 2,
              .failures = 1,
              .last_latency = std::chrono::milliseconds{17},
              .backoff_until = std::chrono::system_clock::now() + std::chrono::seconds{30},
          }},
          .relay_reservations = std::vector<peer_store::relay_record>{peer_store::relay_record{
              .relay = relay_id,
              .reservation_id = 99,
              .expires_at = reservation_expires_at,
              .endpoints = std::vector<fcl::p2p::endpoint>{relay_endpoint},
              .voucher = std::vector<std::uint8_t>{8, 9, 10},
              .successes = 3,
              .failures = 1,
              .last_latency = std::chrono::milliseconds{23},
          }},
          .reachability = reachability::state::publicly_reachable,
          .observed_endpoint = observed,
      });
      store.mark_endpoint_failure(id, endpoint, path::kind::direct,
                                  std::chrono::system_clock::now() + std::chrono::seconds{10});
      store.mark_endpoint_success(id, endpoint, path::kind::direct, std::chrono::milliseconds{11});
   }

   auto reopened = peer_store{peer_store::options{
       .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
   }};
   const auto found = reopened.find(id);
   BOOST_REQUIRE(found.has_value());
   BOOST_TEST(found->protocol_version == "/fcl/reopen/1");
   BOOST_TEST(found->agent_version == "fcl-reopen/1");
   BOOST_TEST(found->public_key == std::vector<std::uint8_t>({1, 2, 3, 4}), boost::test_tools::per_element());
   BOOST_TEST(found->signed_peer_record == std::vector<std::uint8_t>({5, 6, 7}), boost::test_tools::per_element());
   BOOST_TEST(found->observed_endpoint.has_value());
   BOOST_TEST(found->observed_endpoint->transport.port == observed.transport.port);
   BOOST_REQUIRE_EQUAL(found->protocols.size(), 2U);
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_REQUIRE_EQUAL(found->relay_reservations.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.transport.port == endpoint.transport.port);
   BOOST_TEST(found->endpoints.front().successes >= 1U);
   BOOST_TEST(found->endpoints.front().failures >= 1U);
   BOOST_TEST(found->endpoints.front().last_latency == std::chrono::milliseconds{11});
   BOOST_TEST(found->relay_reservations.front().relay.to_string() == relay_id.to_string());
   BOOST_TEST(found->relay_reservations.front().reservation_id == 99U);
   BOOST_REQUIRE_EQUAL(found->relay_reservations.front().endpoints.size(), 1U);
   BOOST_TEST(found->relay_reservations.front().endpoints.front().transport.port == relay_endpoint.transport.port);
   BOOST_TEST(found->relay_reservations.front().voucher == std::vector<std::uint8_t>({8, 9, 10}),
              boost::test_tools::per_element());
   BOOST_TEST(found->relay_reservations.front().successes == 3U);
   BOOST_TEST(found->relay_reservations.front().failures == 1U);
   BOOST_TEST(found->relay_reservations.front().last_latency == std::chrono::milliseconds{23});
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_rocksdb_persists_discovery_dht_and_rendezvous_state) {
   auto temp = temp_store_dir{"rocksdb-discovery"};
   const auto routing_peer = peer(85);
   const auto provider_peer = peer(86);
   const auto rendezvous_peer = peer(87);
   const auto key = make_dht_key(std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'p', 'r', 'o', 'v', 'i', 'd', 'e'});
   const auto routing_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4185/quic-v1/p2p/" + routing_peer.to_string());
   const auto provider_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/4186/quic-v1/p2p/" + provider_peer.to_string());
   const auto rendezvous_endpoint =
       parse_endpoint("/ip4/127.0.0.1/udp/4187/quic-v1/p2p/" + rendezvous_peer.to_string());
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};

   {
      auto store = peer_store{peer_store::options{
          .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
      }};
      store.upsert_routing_peer(
          dht::peer{
              .id = routing_peer,
              .endpoints = std::vector<endpoint>{routing_endpoint},
              .connection = dht::connection_type::can_connect,
          },
          discovery::source::dht, expires_at);
      store.upsert_provider(peer_store::provider_record{
          .key = key,
          .provider =
              dht::peer{
                  .id = provider_peer,
                  .endpoints = std::vector<endpoint>{provider_endpoint},
                  .connection = dht::connection_type::connected,
              },
          .discovered_by = discovery::source::dht,
          .expires_at = expires_at,
          .successes = 2,
      });
      store.upsert_rendezvous(rendezvous::registration{
          .namespace_name = "fcl.discovery",
          .peer = rendezvous_peer,
          .endpoints = std::vector<endpoint>{rendezvous_endpoint},
          .signed_peer_record = std::vector<std::uint8_t>{9, 8, 7},
          .ttl = std::chrono::seconds{7'200},
          .expires_at = expires_at,
      });
   }

   auto reopened = peer_store{peer_store::options{
       .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
   }};
   const auto routing = reopened.closest_routing_peers(key, 10);
   BOOST_REQUIRE_EQUAL(routing.size(), 1U);
   BOOST_TEST(routing.front().id.to_string() == routing_peer.to_string());
   BOOST_REQUIRE_EQUAL(routing.front().endpoints.size(), 1U);
   BOOST_TEST(routing.front().endpoints.front().to_string() == routing_endpoint.to_string());

   const auto providers = reopened.find_providers(key);
   BOOST_REQUIRE_EQUAL(providers.size(), 1U);
   BOOST_TEST(providers.front().provider.id.to_string() == provider_peer.to_string());
   BOOST_REQUIRE_EQUAL(providers.front().provider.endpoints.size(), 1U);
   BOOST_TEST(providers.front().provider.endpoints.front().to_string() == provider_endpoint.to_string());
   BOOST_TEST(providers.front().successes == 2U);

   const auto registrations = reopened.discover_rendezvous("fcl.discovery", 0, 10);
   BOOST_REQUIRE_EQUAL(registrations.size(), 1U);
   BOOST_TEST(registrations.front().peer.to_string() == rendezvous_peer.to_string());
   BOOST_TEST(registrations.front().namespace_name == "fcl.discovery");
   BOOST_TEST(registrations.front().signed_peer_record == std::vector<std::uint8_t>({9, 8, 7}),
              boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(registrations.front().endpoints.size(), 1U);
   BOOST_TEST(registrations.front().endpoints.front().to_string() == rendezvous_endpoint.to_string());
   BOOST_TEST(reopened.discover_rendezvous("fcl.discovery", registrations.front().sequence, 10).empty());
}
#else
BOOST_AUTO_TEST_CASE(p2p_peer_store_rocksdb_backend_reports_disabled_when_not_built) {
   BOOST_CHECK_THROW((void)peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = "peer-store"}),
                     exceptions::invalid_options);
}
#endif

BOOST_AUTO_TEST_CASE(p2p_production_options_reject_missing_mtls_identity) {
   try {
      validate(node::options{});
      BOOST_FAIL("expected missing mTLS identity rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }
}

BOOST_AUTO_TEST_CASE(p2p_production_options_require_peer_store_path) {
   try {
      validate(node::options{
          .certificate_pem = std::string{test_certificate()},
          .private_key_pem = std::string{test_private_key()},
      });
      BOOST_FAIL("expected missing persistent peer store rejection");
   } catch (const fcl::exceptions::base& error) {
      BOOST_TEST(static_cast<int>(fcl::p2p::exceptions::code_of(error).value()) ==
                 static_cast<int>(exceptions::code::invalid_options));
   }
}

#if FCL_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(p2p_production_options_use_rocksdb_peer_store_path) {
   auto temp = temp_store_dir{"node-rocksdb-path"};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   const auto id = peer(83);
   {
      auto value = node{runtime, node::options{
                                     .certificate_pem = std::string{test_certificate()},
                                     .private_key_pem = std::string{test_private_key()},
                                     .explicit_peer_id = id,
                                     .peer_store_path = temp.path(),
                                 }};
      value.peers().upsert(peer_store::record{
          .peer = peer(84),
          .protocol_version = "/fcl/node-rocksdb/1",
          .agent_version = "fcl-node-rocksdb/1",
          .protocols = std::vector<protocol_id>{builtins::identify_push},
      });
      fcl::asio::blocking::run(runtime, value.async_stop());
   }
   {
      auto value = node{runtime, node::options{
                                     .certificate_pem = std::string{test_certificate()},
                                     .private_key_pem = std::string{test_private_key()},
                                     .explicit_peer_id = id,
                                     .peer_store_path = temp.path(),
                                 }};
      const auto found = value.peers().find(peer(84));
      BOOST_REQUIRE(found.has_value());
      BOOST_TEST(found->protocol_version == "/fcl/node-rocksdb/1");
      fcl::asio::blocking::run(runtime, value.async_stop());
   }
}
#endif

} // namespace fcl::p2p
