#include <chrono>
#include <coroutine>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.crypto.pem;
import forge.p2p.dht;
import forge.multiformats.exceptions;
import forge.multiformats.multihash;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.p2p.endpoint;
import forge.p2p.envelope;
import forge.p2p.exceptions;
import forge.p2p.hole_punch;
import forge.p2p.identify;
import forge.p2p.identity;
import forge.p2p.node;
import forge.p2p.protocol;
import forge.p2p.pubsub;
import forge.p2p.reachability;
import forge.p2p.rendezvous;
import forge.p2p.relay;
import forge.p2p.stream;

namespace {

using namespace std::chrono_literals;

constexpr auto echo_protocol = std::string_view{"/forge/interop/relay-echo/1"};
constexpr auto rendezvous_namespace = std::string_view{"forge.discovery"};
constexpr auto pubsub_topic = std::string_view{"forge.pubsub.interop"};
constexpr auto pubsub_payload = std::string_view{"forge-gossipsub-live"};

struct bio_deleter {
   void operator()(BIO* value) const noexcept {
      BIO_free(value);
   }
};

struct evp_md_ctx_deleter {
   void operator()(EVP_MD_CTX* value) const noexcept {
      EVP_MD_CTX_free(value);
   }
};

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
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

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

struct x509_extension_deleter {
   void operator()(X509_EXTENSION* value) const noexcept {
      X509_EXTENSION_free(value);
   }
};

struct libp2p_identity {
   std::string certificate_pem;
   std::string private_key_pem;
   std::vector<std::uint8_t> public_key;
   forge::p2p::peer_id peer;
};

std::string json_escape(std::string_view value) {
   auto out = std::string{};
   out.reserve(value.size() + 8);
   for (const auto ch : value) {
      switch (ch) {
      case '\\':
         out += "\\\\";
         break;
      case '"':
         out += "\\\"";
         break;
      case '\n':
         out += "\\n";
         break;
      case '\r':
         out += "\\r";
         break;
      case '\t':
         out += "\\t";
         break;
      default:
         out.push_back(ch);
         break;
      }
   }
   return out;
}

std::map<std::string, std::string> parse_args(int argc, char** argv) {
   auto out = std::map<std::string, std::string>{};
   if (argc < 2) {
      throw std::runtime_error{"missing command"};
   }
   out["command"] = argv[1];
   for (auto i = 2; i < argc; ++i) {
      auto key = std::string{argv[i]};
      if (!key.starts_with("--")) {
         throw std::runtime_error{"unexpected positional argument: " + key};
      }
      if (i + 1 >= argc) {
         throw std::runtime_error{"missing value for " + key};
      }
      out[key.substr(2)] = argv[++i];
   }
   return out;
}

const std::string& required(const std::map<std::string, std::string>& args, std::string_view key) {
   const auto it = args.find(std::string{key});
   if (it == args.end() || it->second.empty()) {
      throw std::runtime_error{"missing required argument --" + std::string{key}};
   }
   return it->second;
}

std::string optional_value(const std::map<std::string, std::string>& args, std::string_view key,
                           std::string_view fallback = {}) {
   const auto it = args.find(std::string{key});
   if (it == args.end() || it->second.empty()) {
      return std::string{fallback};
   }
   return it->second;
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
   auto in = std::ifstream{path};
   if (!in) {
      throw std::runtime_error{"failed to open " + path.string()};
   }
   auto out = std::vector<std::string>{};
   for (auto line = std::string{}; std::getline(in, line);) {
      if (!line.empty()) {
         out.push_back(line);
      }
   }
   return out;
}

void write_file(const std::filesystem::path& path, std::string_view value) {
   std::filesystem::create_directories(path.parent_path());
   auto out = std::ofstream{path};
   if (!out) {
      throw std::runtime_error{"failed to open " + path.string()};
   }
   out << value;
}

void require_openssl(bool ok, std::string_view message) {
   if (!ok) {
      throw std::runtime_error{std::string{message}};
   }
}

std::string bio_to_string(BIO* bio) {
   BUF_MEM* memory = nullptr;
   BIO_get_mem_ptr(bio, &memory);
   if (memory == nullptr) {
      throw std::runtime_error{"failed to read OpenSSL memory BIO"};
   }
   return std::string{memory->data, memory->length};
}

std::vector<std::uint8_t> public_key_spki_der(EVP_PKEY* key) {
   const auto length = i2d_PUBKEY(key, nullptr);
   require_openssl(length > 0, "failed to DER-encode libp2p certificate public key");
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   require_openssl(i2d_PUBKEY(key, &cursor) == length, "failed to DER-write libp2p certificate public key");
   return out;
}

std::vector<std::uint8_t> raw_public_key(EVP_PKEY* key) {
   auto size = std::size_t{};
   require_openssl(EVP_PKEY_get_raw_public_key(key, nullptr, &size) == 1 && size != 0,
                   "failed to size libp2p identity public key");
   auto out = std::vector<std::uint8_t>(size);
   require_openssl(EVP_PKEY_get_raw_public_key(key, out.data(), &size) == 1,
                   "failed to read libp2p identity public key");
   out.resize(size);
   return out;
}

std::vector<std::uint8_t> identity_sign(EVP_PKEY* key, std::span<const std::uint8_t> message) {
   auto context = std::unique_ptr<EVP_MD_CTX, evp_md_ctx_deleter>{EVP_MD_CTX_new()};
   require_openssl(context != nullptr, "failed to allocate libp2p signing context");
   require_openssl(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key) == 1,
                   "failed to initialize libp2p certificate extension signer");
   auto size = std::size_t{};
   require_openssl(EVP_DigestSign(context.get(), nullptr, &size, message.data(), message.size()) == 1,
                   "failed to size libp2p certificate extension signature");
   auto out = std::vector<std::uint8_t>(size);
   require_openssl(EVP_DigestSign(context.get(), out.data(), &size, message.data(), message.size()) == 1,
                   "failed to sign libp2p certificate extension");
   out.resize(size);
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

libp2p_identity generate_libp2p_identity() {
   auto key = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>{EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")};
   require_openssl(key != nullptr, "failed to generate libp2p Ed25519 identity key");

   const auto spki = public_key_spki_der(key.get());
   auto signed_message = std::vector<std::uint8_t>{};
   constexpr auto prefix = std::string_view{"libp2p-tls-handshake:"};
   signed_message.insert(signed_message.end(), prefix.begin(), prefix.end());
   signed_message.insert(signed_message.end(), spki.begin(), spki.end());
   const auto signature = identity_sign(key.get(), signed_message);
   const auto public_key = forge::p2p::encode_public_key(forge::p2p::public_key{
       .type = forge::p2p::public_key::type::ed25519,
       .data = raw_public_key(key.get()),
   });
   const auto extension_value = signed_key_der(public_key, signature);

   auto certificate = std::unique_ptr<X509, x509_deleter>{X509_new()};
   require_openssl(certificate != nullptr, "failed to allocate libp2p certificate");
   require_openssl(X509_set_version(certificate.get(), 2) == 1, "failed to set libp2p certificate version");
   require_openssl(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1,
                   "failed to set libp2p certificate serial");
   require_openssl(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) != nullptr,
                   "failed to set libp2p certificate notBefore");
   require_openssl(X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 100L * 365L * 24L * 60L * 60L) != nullptr,
                   "failed to set libp2p certificate notAfter");
   require_openssl(X509_set_pubkey(certificate.get(), key.get()) == 1, "failed to set libp2p certificate key");
   auto* name = X509_get_subject_name(certificate.get());
   require_openssl(name != nullptr, "failed to allocate libp2p certificate subject");
   const auto serial = std::string{"forge-libp2p-interop"};
   require_openssl(X509_NAME_add_entry_by_txt(name, "serialNumber", MBSTRING_ASC,
                                              reinterpret_cast<const unsigned char*>(serial.data()),
                                              static_cast<int>(serial.size()), -1, 0) == 1,
                   "failed to set libp2p certificate subject");
   require_openssl(X509_set_issuer_name(certificate.get(), name) == 1, "failed to set libp2p certificate issuer");

   auto object =
       std::unique_ptr<ASN1_OBJECT, asn1_object_deleter>{OBJ_txt2obj("1.3.6.1.4.1.53594.1.1", 1)};
   require_openssl(object != nullptr, "failed to create libp2p extension OID");
   auto octets = std::unique_ptr<ASN1_OCTET_STRING, asn1_octet_string_deleter>{ASN1_OCTET_STRING_new()};
   require_openssl(octets != nullptr, "failed to allocate libp2p extension value");
   require_openssl(ASN1_OCTET_STRING_set(octets.get(), extension_value.data(),
                                         static_cast<int>(extension_value.size())) == 1,
                   "failed to set libp2p extension value");
   auto extension = std::unique_ptr<X509_EXTENSION, x509_extension_deleter>{
       X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 1, octets.get())};
   require_openssl(extension != nullptr, "failed to create libp2p public key extension");
   require_openssl(X509_add_ext(certificate.get(), extension.get(), -1) == 1,
                   "failed to add libp2p public key extension");
   require_openssl(X509_sign(certificate.get(), key.get(), nullptr) > 0, "failed to sign libp2p certificate");

   auto certificate_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   auto private_key_bio = std::unique_ptr<BIO, bio_deleter>{BIO_new(BIO_s_mem())};
   require_openssl(certificate_bio != nullptr && private_key_bio != nullptr, "failed to allocate libp2p PEM BIO");
   require_openssl(PEM_write_bio_X509(certificate_bio.get(), certificate.get()) == 1,
                   "failed to write libp2p certificate PEM");
   require_openssl(PEM_write_bio_PrivateKey(private_key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) ==
                       1,
                   "failed to write libp2p private key PEM");

   return libp2p_identity{
       .certificate_pem = bio_to_string(certificate_bio.get()),
       .private_key_pem = bio_to_string(private_key_bio.get()),
       .public_key = public_key,
       .peer = forge::p2p::make_peer_id(forge::p2p::public_key{
           .type = forge::p2p::public_key::type::ed25519,
           .data = raw_public_key(key.get()),
       }),
   };
}

const libp2p_identity& local_identity() {
   static const auto identity = generate_libp2p_identity();
   return identity;
}

forge::p2p::node::options node_options(const std::filesystem::path& store_path, const libp2p_identity& identity) {
   auto out = forge::p2p::node::options{
       .certificate_pem = identity.certificate_pem,
       .private_key_pem = identity.private_key_pem,
       .explicit_peer_id = identity.peer,
       .capabilities = forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                        forge::p2p::capabilities::peer_exchange |
                                                        forge::p2p::capabilities::autonat |
                                                        forge::p2p::capabilities::relay |
                                                        forge::p2p::capabilities::hole_punching |
                                                        forge::p2p::capabilities::relay_reservation |
                                                        forge::p2p::capabilities::dht |
                                                        forge::p2p::capabilities::rendezvous |
                                                        forge::p2p::capabilities::pubsub},
       .public_key = identity.public_key,
       .peer_store_path = store_path,
       .allow_insecure_test_mode = true,
   };
   out.limits.dht.operating_mode = forge::p2p::dht::mode::server;
   out.limits.rendezvous.operating_role = forge::p2p::rendezvous::role::client_and_server;
   return out;
}

forge::p2p::node::options node_options(const std::filesystem::path& store_path) {
   return node_options(store_path, local_identity());
}

std::string endpoint_json(const forge::p2p::endpoint& endpoint) {
   return "\"" + json_escape(endpoint.to_string()) + "\"";
}

forge::p2p::endpoint loopback_quic_endpoint(std::uint16_t port = 0) {
   return forge::p2p::endpoint{.transport = {.host_type = forge::p2p::endpoint::host_kind::ip4,
                                           .protocol = forge::p2p::endpoint::protocol_kind::quic_v1,
                                           .host = "127.0.0.1",
                                           .port = port}};
}

forge::p2p::endpoint loopback_tcp_endpoint(std::uint16_t port = 0) {
   return forge::p2p::endpoint{.transport = {.host_type = forge::p2p::endpoint::host_kind::ip4,
                                           .protocol = forge::p2p::endpoint::protocol_kind::tcp,
                                           .host = "127.0.0.1",
                                           .port = port}};
}

forge::p2p::endpoint loopback_endpoint_for(std::string_view transport) {
   if (transport == "tcp" || transport == "tcp-tls") {
      return loopback_tcp_endpoint();
   }
   if (transport == "quic" || transport.empty()) {
      return loopback_quic_endpoint();
   }
   throw std::runtime_error{"unsupported FORGE fixture transport: " + std::string{transport}};
}

forge::p2p::endpoint p2p_endpoint_for(forge::p2p::endpoint value, const forge::p2p::peer_id& peer) {
   value.peer = peer;
   return value;
}

forge::p2p::dht::key provider_key() {
   constexpr auto source = std::string_view{"forge-libp2p-dht-provider"};
   auto digest = forge::multiformats::multihash::sha2_256(
       std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(source.data()), source.size()});
   return forge::p2p::dht::key{.bytes = digest.encode()};
}

std::vector<std::uint8_t> signed_rendezvous_record(const libp2p_identity& identity, const forge::p2p::endpoint& endpoint) {
   const auto key = forge::p2p::decode_public_key(identity.public_key);
   return forge::p2p::rendezvous::codec::seal_peer_record(
              forge::p2p::rendezvous::peer_record{
                  .peer = identity.peer,
                  .endpoints = std::vector<forge::p2p::endpoint>{endpoint},
                  .sequence = static_cast<std::uint64_t>(
                      std::chrono::system_clock::now().time_since_epoch().count()),
              },
              key, forge::crypto::pem::read_private_key(identity.private_key_pem))
       .encode();
}

std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload);
boost::asio::awaitable<std::vector<std::uint8_t>>
read_length_delimited(forge::p2p::stream& stream, std::size_t max_payload_size);

void register_echo(forge::p2p::node& value) {
   value.register_protocol_handler(forge::p2p::protocol_id{.value = std::string{echo_protocol}},
                                   [](forge::p2p::node::incoming_protocol_stream incoming)
                                       -> boost::asio::awaitable<void> {
                                      auto payload = co_await read_length_delimited(incoming.stream, 16 * 1024);
                                      co_await incoming.stream.async_write(wrap_length_delimited(payload));
                                   });
}

struct pubsub_stress_state {
   std::mutex mutex;
   std::set<std::string> payloads;
   std::uint64_t duplicates = 0;
   std::uint64_t invalid = 0;
};

void register_pubsub_listener(forge::asio::runtime& runtime, forge::p2p::node& value, std::filesystem::path result_file) {
   forge::asio::blocking::run(
       runtime, value.async_subscribe(
                    forge::p2p::pubsub::topic{.value = std::string{pubsub_topic}},
                    [result_file = std::move(result_file)](
                        forge::p2p::pubsub::event event) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
                       const auto payload = std::string{event.value.data.begin(), event.value.data.end()};
                       write_file(result_file,
                                  "{\"implementation\":\"forge\",\"scenario\":\"gossipsub_publish\",\"status\":\"" +
                                      std::string{payload == pubsub_payload ? "ok" : "mismatch"} +
                                      "\",\"topic\":\"" + json_escape(event.value.subject.value) + "\",\"payload\":\"" +
                                      json_escape(payload) + "\",\"source\":\"" +
                                      json_escape(event.value.from ? event.value.from->to_string() : std::string{}) +
                                      "\"}\n");
                       co_return forge::p2p::pubsub::validation_result::accept;
                    }));
}

std::shared_ptr<pubsub_stress_state> register_pubsub_stress_listener(forge::asio::runtime& runtime,
                                                                      forge::p2p::node& value) {
   auto state = std::make_shared<pubsub_stress_state>();
   forge::asio::blocking::run(
       runtime, value.async_subscribe(
                    forge::p2p::pubsub::topic{.value = std::string{pubsub_topic}},
                    [state](forge::p2p::pubsub::event event)
                        -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
                       const auto payload = std::string{event.value.data.begin(), event.value.data.end()};
                       auto lock = std::scoped_lock{state->mutex};
                       if (!state->payloads.insert(payload).second) {
                          ++state->duplicates;
                       }
                       co_return forge::p2p::pubsub::validation_result::accept;
                    }));
   return state;
}

void prepare_pubsub_publisher(forge::asio::runtime& runtime, forge::p2p::node& value) {
   forge::asio::blocking::run(
       runtime, value.async_subscribe(
                    forge::p2p::pubsub::topic{.value = std::string{pubsub_topic}},
                    [](forge::p2p::pubsub::event) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
                       co_return forge::p2p::pubsub::validation_result::accept;
                    }));

   const auto deadline = std::chrono::steady_clock::now() + 8s;
   while (std::chrono::steady_clock::now() < deadline) {
      const auto snapshot = value.pubsub_snapshot();
      if (snapshot.peers > 0 || snapshot.mesh_edges > 0) {
         return;
      }
      std::this_thread::sleep_for(100ms);
   }
}

void write_pubsub_stress_result(const std::filesystem::path& result_file, std::string_view implementation,
                                const pubsub_stress_state& state, std::uint64_t expected, const forge::p2p::node& value) {
   auto payloads = std::string{};
   auto first = true;
   for (const auto& payload : state.payloads) {
      if (!first) {
         payloads += ",";
      }
      first = false;
      payloads += "\"" + json_escape(payload) + "\"";
   }
   const auto metrics = value.metrics();
   write_file(result_file,
              "{\"implementation\":\"" + std::string{implementation} +
                  "\",\"scenario\":\"gossipsub_mixed_mesh_stress\",\"status\":\"" +
                  std::string{state.payloads.size() >= expected && state.duplicates == 0 ? "ok" : "mismatch"} +
                  "\",\"received\":" + std::to_string(state.payloads.size()) +
                  ",\"expected\":" + std::to_string(expected) +
                  ",\"duplicates\":" + std::to_string(state.duplicates) +
                  ",\"invalid\":" + std::to_string(metrics.pubsub_invalid_messages) +
                  ",\"rejected\":" + std::to_string(metrics.protocol_rejections) +
                  ",\"payloads\":[" + payloads + "]}\n");
}

std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes, std::size_t max_payload_size) {
   auto decoded = forge::multiformats::decoded_varint{};
   try {
      decoded = forge::multiformats::varint_decode(bytes);
   } catch (const std::exception& error) {
      throw std::runtime_error{std::string{"failed to decode libp2p protobuf length: "} + error.what()};
   }
   if (decoded.value > max_payload_size) {
      throw std::runtime_error{"libp2p protobuf message exceeds max size"};
   }
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   if (total != bytes.size()) {
      throw std::runtime_error{"libp2p protobuf message length mismatch"};
   }
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload) {
   auto out = forge::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

boost::asio::awaitable<std::vector<std::uint8_t>>
read_length_delimited(forge::p2p::stream& stream, std::size_t max_payload_size) {
   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      try {
         const auto decoded = forge::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            throw std::runtime_error{"libp2p protobuf message exceeds max size"};
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto frame = std::vector<std::uint8_t>{buffer.begin(),
                                                   buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            co_return unwrap_length_delimited(frame, max_payload_size);
         }
      } catch (const forge::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            throw;
         }
      }
      auto chunk = co_await stream.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

int listen_mode(const std::map<std::string, std::string>& args) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto value = forge::p2p::node{runtime, node_options(required(args, "store-dir"))};
   register_echo(value);
   const auto scenario = optional_value(args, "scenario");
   auto stress_state = std::shared_ptr<pubsub_stress_state>{};
   if (scenario == "gossipsub_publish") {
      register_pubsub_listener(runtime, value, required(args, "result-file"));
   } else if (scenario == "gossipsub_mixed_mesh_stress") {
      stress_state = register_pubsub_stress_listener(runtime, value);
   }
   const auto transport = optional_value(args, "transport", "quic");
   forge::asio::blocking::run(runtime, value.async_listen(loopback_endpoint_for(transport)));
   const auto local = value.local_endpoint();
   if (!local) {
      throw std::runtime_error{"FORGE fixture did not expose a local endpoint"};
   }
   const auto endpoint = p2p_endpoint_for(*local, value.local_peer());
   write_file(required(args, "ready-file"),
              "{\"implementation\":\"forge\",\"role\":\"listener\",\"peer_id\":\"" +
                  json_escape(value.local_peer().to_string()) + "\",\"listen_addrs\":[" + endpoint_json(endpoint) +
                  "],\"status\":\"ready\"}\n");

   const auto stop_file = std::filesystem::path{required(args, "stop-file")};
   auto seeded = false;
   while (!std::filesystem::exists(stop_file)) {
      if (!seeded && scenario == "gossipsub_mixed_mesh_stress") {
         const auto seed_file = std::filesystem::path{required(args, "seed-file")};
         if (std::filesystem::exists(seed_file)) {
            for (const auto& line : read_lines(seed_file)) {
               const auto remote = forge::p2p::parse_endpoint(line);
               if (!remote.peer || *remote.peer == value.local_peer()) {
                  continue;
               }
               value.peers().learn_endpoint(*remote.peer, remote,
                                            forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                                             forge::p2p::capabilities::pubsub});
               try {
                  (void)forge::asio::blocking::run(
                      runtime, value.async_connect(remote,
                                                   forge::p2p::node::connect_options{.expected_peer = *remote.peer,
                                                                                   .allow_relay = false,
                                                                                   .allow_hole_punch = false}));
               } catch (const forge::exceptions::base&) {
               }
            }
            seeded = true;
         }
      }
      std::this_thread::sleep_for(100ms);
   }
   const auto metrics = value.metrics();
   std::cerr << "forge listener metrics:"
             << " sessions_opened=" << metrics.sessions_opened
             << " handshakes_completed=" << metrics.handshakes_completed
             << " handshakes_failed=" << metrics.handshakes_failed
             << " protocol_streams_accepted=" << metrics.protocol_streams_accepted
             << " protocol_rejections=" << metrics.protocol_rejections
             << " pubsub_received=" << metrics.pubsub_messages_received
             << " pubsub_delivered=" << metrics.pubsub_messages_delivered
             << " pubsub_invalid=" << metrics.pubsub_invalid_messages
             << " pubsub_duplicates=" << metrics.pubsub_duplicates << "\n";
   if (stress_state) {
      auto lock = std::scoped_lock{stress_state->mutex};
      write_pubsub_stress_result(required(args, "result-file"), "forge", *stress_state,
                                 static_cast<std::uint64_t>(std::stoull(optional_value(args, "expected-messages", "3"))),
                                 value);
   }
   forge::asio::blocking::run(runtime, value.async_stop());
   return 0;
}

int destination_mode(const std::map<std::string, std::string>& args) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto value = forge::p2p::node{runtime, node_options(required(args, "store-dir"))};
   register_echo(value);
   forge::asio::blocking::run(runtime, value.async_listen(loopback_quic_endpoint()));

   const auto relay_addr = forge::p2p::parse_endpoint(required(args, "relay-addr"));
   const auto relay_peer = forge::p2p::peer_id::from_string(required(args, "relay-peer-id"));
   value.peers().learn_endpoint(relay_peer, relay_addr,
                                forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                                 forge::p2p::capabilities::relay |
                                                                 forge::p2p::capabilities::relay_reservation});
   const auto reservation = forge::asio::blocking::run(runtime, value.async_reserve_relay(relay_peer));
   const auto local = value.local_endpoint();
   if (!local) {
      throw std::runtime_error{"FORGE destination did not expose a local endpoint"};
   }
   auto relay_addrs = std::string{};
   for (std::size_t i = 0; i < reservation.relay_endpoints.size(); ++i) {
      if (i != 0) {
         relay_addrs += ",";
      }
      relay_addrs += endpoint_json(reservation.relay_endpoints[i]);
   }
   write_file(required(args, "ready-file"),
              "{\"implementation\":\"forge\",\"role\":\"destination\",\"peer_id\":\"" +
                  json_escape(value.local_peer().to_string()) + "\",\"listen_addrs\":[" +
                  endpoint_json(p2p_endpoint_for(*local, value.local_peer())) + "],\"relay_addrs\":[" +
                  relay_addrs + "],\"relay_peer_id\":\"" + json_escape(relay_peer.to_string()) +
                  "\",\"native_relay_transport\":true,\"voucher\":" +
                  std::string{reservation.voucher ? "true" : "false"} + ",\"status\":\"ready\"}\n");

   const auto stop_file = std::filesystem::path{required(args, "stop-file")};
   while (!std::filesystem::exists(stop_file)) {
      std::this_thread::sleep_for(100ms);
   }
   forge::asio::blocking::run(runtime, value.async_stop());
   return 0;
}

std::string run_scenario(forge::asio::runtime& runtime, forge::p2p::node& value, std::string_view scenario,
                         std::string_view payload,
                         const forge::p2p::peer_id& peer) {
   if (scenario == "ping") {
      const auto rtt = forge::asio::blocking::run(runtime, value.async_ping(
                                                        peer, forge::p2p::node::open_options{.allow_relay = false,
                                                                                            .allow_hole_punch = false}));
      return "\"rtt_ms\":" + std::to_string(rtt.count());
   }
   if (scenario == "identify") {
      auto document = forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<forge::p2p::identify::document> {
         auto stream = co_await value.async_open_protocol_stream(peer, forge::p2p::builtins::identify,
                                                                 forge::p2p::node::open_options{.allow_relay = false});
         auto payload = co_await read_length_delimited(stream, 4 * 1024 * 1024);
         co_return forge::p2p::identify::decode(payload);
      }());
      return "\"protocol_count\":" + std::to_string(document.protocols.size()) + ",\"agent_version\":\"" +
             json_escape(document.agent_version) + "\"";
   }
   if (scenario == "autonatv2") {
      const auto state = forge::asio::blocking::run(runtime, value.async_probe_reachability(peer));
      return "\"reachability\":" + std::to_string(static_cast<int>(state));
   }
   if (scenario == "relay_reserve") {
      const auto reservation = forge::asio::blocking::run(runtime, value.async_reserve_relay(peer));
      return "\"voucher_bytes\":" + std::to_string(reservation.voucher ? reservation.voucher->encode().size() : 0U);
   }
   if (scenario == "dht_find_peer") {
      const auto result = forge::asio::blocking::run(runtime, value.async_find_peer(peer));
      return "\"closest_peers\":" + std::to_string(result.closest_peers.size()) +
             ",\"complete\":" + std::string{result.complete ? "true" : "false"};
   }
   if (scenario == "dht_provide_find_provider") {
      const auto key = provider_key();
      forge::asio::blocking::run(runtime, value.async_provide(key));
      auto stream = forge::asio::blocking::run(runtime, value.async_open_protocol_stream(
                                                         peer, forge::p2p::builtins::kad_dht,
                                                         forge::p2p::node::open_options{.allow_relay = false}));
      forge::asio::blocking::run(runtime, stream.async_write(forge::p2p::dht::codec::encode(
                                          forge::p2p::dht::message{
                                              .type = forge::p2p::dht::message_type::get_providers,
                                              .key_value = key,
                                          },
                                          forge::p2p::dht::options{})));
      const auto response = forge::p2p::dht::codec::decode(
          wrap_length_delimited(forge::asio::blocking::run(runtime, read_length_delimited(stream, 1024 * 1024))));
      return "\"provider_count\":" + std::to_string(response.provider_peers.size());
   }
   if (scenario == "rendezvous_register_discover") {
      const auto local = value.local_endpoint();
      if (!local) {
         throw std::runtime_error{"FORGE rendezvous scenario requires local endpoint"};
      }
      const auto endpoint = p2p_endpoint_for(*local, value.local_peer());
      const auto record = signed_rendezvous_record(local_identity(), endpoint);
      const auto registration = forge::asio::blocking::run(runtime, value.async_rendezvous_register(
                                                                      peer,
                                                                      forge::p2p::rendezvous::register_request{
                                                                          .namespace_name =
                                                                              std::string{rendezvous_namespace},
                                                                          .signed_peer_record = record,
                                                                          .ttl = std::chrono::seconds{7'200},
                                                                      }));
      if (registration.status_value != forge::p2p::rendezvous::status::ok) {
         throw std::runtime_error{"rendezvous register failed"};
      }
      const auto discovered = forge::asio::blocking::run(runtime, value.async_rendezvous_discover(
                                                                    peer,
                                                                    forge::p2p::rendezvous::discover_request{
                                                                        .namespace_name =
                                                                            std::string{rendezvous_namespace},
                                                                        .limit = 10,
                                                                    }));
      return "\"registration_count\":" + std::to_string(discovered.registrations.size()) +
             ",\"cookie_bytes\":" + std::to_string(discovered.cookie.size());
   }
   if (scenario == "gossipsub_publish" || scenario == "gossipsub_mixed_mesh_stress") {
      prepare_pubsub_publisher(runtime, value);
      const auto message = forge::asio::blocking::run(runtime, value.async_publish(
                                                                forge::p2p::pubsub::topic{
                                                                    .value = std::string{pubsub_topic},
                                                                },
                                                                std::vector<std::uint8_t>{
                                                                    payload.begin(), payload.end()}));
      std::this_thread::sleep_for(2s);
      return "\"topic\":\"" + json_escape(message.subject.value) + "\",\"payload_bytes\":" +
             std::to_string(message.data.size()) + ",\"signed\":" +
             std::string{message.signature.empty() ? "false" : "true"};
   }
   if (scenario == "echo") {
      auto stream = forge::asio::blocking::run(runtime, value.async_open_protocol_stream(
                                                         peer,
                                                         forge::p2p::protocol_id{.value = std::string{echo_protocol}},
                                                         forge::p2p::node::open_options{.allow_relay = false,
                                                                                      .allow_hole_punch = false}));
      const auto bytes = std::vector<std::uint8_t>{payload.begin(), payload.end()};
      forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(bytes)));
      const auto echoed = forge::asio::blocking::run(runtime, read_length_delimited(stream, 16 * 1024));
      if (echoed != bytes) {
         throw std::runtime_error{"FORGE echo mismatch"};
      }
      return "\"protocol\":\"" + json_escape(echo_protocol) + "\",\"payload_bytes\":" +
             std::to_string(echoed.size()) + ",\"echo_ok\":true";
   }
   if (scenario == "dcutr") {
      const auto status = forge::asio::blocking::run(runtime, value.async_attempt_hole_punch(peer));
      return "\"hole_punch_status\":" + std::to_string(static_cast<int>(status));
   }
   if (scenario == "unknown_protocol") {
      try {
         (void)forge::asio::blocking::run(runtime, value.async_open_protocol_stream(
                                                     peer, forge::p2p::protocol_id{.value = "/forge/interop/unknown/1"},
                                                     forge::p2p::node::open_options{.allow_relay = false,
                                                                                  .allow_hole_punch = false}));
      } catch (const forge::exceptions::base& error) {
         if (forge::p2p::exceptions::code_of(error).value() != forge::p2p::exceptions::code::unsupported_protocol) {
            throw;
         }
         return "\"expected_error\":\"" + json_escape(error.what()) + "\"";
      }
      throw std::runtime_error{"unknown protocol unexpectedly succeeded"};
   }
   throw std::runtime_error{"unknown FORGE fixture scenario: " + std::string{scenario}};
}

int dial_mode(const std::map<std::string, std::string>& args) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto value = forge::p2p::node{runtime, node_options(required(args, "store-dir"))};
   forge::asio::blocking::run(runtime, value.async_listen(loopback_quic_endpoint()));

   auto remote = forge::p2p::parse_endpoint(required(args, "addr"));
   auto peer = forge::p2p::peer_id::from_string(required(args, "peer-id"));
   value.peers().learn_endpoint(peer, remote,
                                forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                                 forge::p2p::capabilities::peer_exchange |
                                                                 forge::p2p::capabilities::autonat |
                                                                 forge::p2p::capabilities::relay |
                                                                 forge::p2p::capabilities::hole_punching |
                                                                 forge::p2p::capabilities::relay_reservation |
                                                                 forge::p2p::capabilities::dht |
                                                                  forge::p2p::capabilities::rendezvous |
                                                                  forge::p2p::capabilities::pubsub});

   const auto scenario = required(args, "scenario");
   if (scenario == "gossipsub_publish" || scenario == "gossipsub_mixed_mesh_stress") {
      (void)forge::asio::blocking::run(
          runtime, value.async_connect(remote,
                                       forge::p2p::node::connect_options{.expected_peer = peer,
                                                                       .allow_relay = false,
                                                                       .allow_hole_punch = false}));
   }

   const auto details = run_scenario(runtime, value, scenario, optional_value(args, "payload", pubsub_payload), peer);
   forge::asio::blocking::run(runtime, value.async_stop());
   write_file(required(args, "result-file"),
              "{\"implementation\":\"forge\",\"role\":\"dialer\",\"scenario\":\"" + json_escape(scenario) +
                  "\",\"status\":\"ok\"," + details + "}\n");
   return 0;
}

int dial_relay_mode(const std::map<std::string, std::string>& args) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto value = forge::p2p::node{runtime, node_options(required(args, "store-dir"))};
   forge::asio::blocking::run(runtime, value.async_listen(loopback_quic_endpoint()));

   const auto relay_addr = forge::p2p::parse_endpoint(required(args, "relay-addr"));
   const auto relay_peer = forge::p2p::peer_id::from_string(required(args, "relay-peer-id"));
   const auto target_peer = forge::p2p::peer_id::from_string(required(args, "peer-id"));
   value.peers().learn_endpoint(relay_peer, relay_addr,
                                forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                                 forge::p2p::capabilities::relay |
                                                                 forge::p2p::capabilities::relay_reservation |
                                                                 forge::p2p::capabilities::hole_punching});

   auto status = forge::p2p::hole_punch::status::failed;
   auto relay_echo = false;
   if (required(args, "scenario") == "relay_echo_topology") {
      auto stream = forge::asio::blocking::run(runtime, value.async_open_protocol_stream(
                                                         target_peer,
                                                         forge::p2p::protocol_id{.value = std::string{echo_protocol}},
                                                         forge::p2p::node::open_options{
                                                             .allow_relay = true,
                                                             .relay_peer = relay_peer,
                                                             .timeout = 15s,
                                                             .allow_hole_punch = false,
                                                         }));
      const auto payload = std::vector<std::uint8_t>{'r', 'e', 'l', 'a', 'y', '-', 'e', 'c', 'h', 'o'};
      forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(payload)));
      const auto echoed = forge::asio::blocking::run(runtime, read_length_delimited(stream, 16 * 1024));
      if (echoed != payload) {
         throw std::runtime_error{"FORGE relay echo mismatch"};
      }
      relay_echo = true;
   } else {
      status = forge::asio::blocking::run(runtime, value.async_attempt_hole_punch(target_peer, relay_peer, 15s));
      relay_echo = status == forge::p2p::hole_punch::status::succeeded;
   }
   const auto metrics = value.metrics();
   forge::asio::blocking::run(runtime, value.async_stop());
   write_file(required(args, "result-file"),
              "{\"implementation\":\"forge\",\"role\":\"relay_dialer\",\"scenario\":\"" +
                  json_escape(required(args, "scenario")) + "\",\"status\":\"ok\",\"relay_peer\":\"" +
                  json_escape(relay_peer.to_string()) + "\",\"target_peer\":\"" +
                  json_escape(target_peer.to_string()) + "\",\"hole_punch_status\":" +
                  std::to_string(static_cast<int>(status)) + ",\"hole_punch_successes\":" +
                  std::to_string(metrics.hole_punch_successes) + ",\"hole_punch_failures\":" +
                  std::to_string(metrics.hole_punch_failures) + ",\"path_relay_attempts\":" +
                  std::to_string(metrics.path_relay_attempts) + ",\"path_relay_opens\":" +
                  std::to_string(metrics.path_relay_opens) + ",\"relay_failures\":" +
                  std::to_string(metrics.relay_failures) + ",\"direct_failures\":" +
                  std::to_string(metrics.direct_failures) + ",\"relay_echo\":" +
                  std::string{relay_echo ? "true" : "false"} + ",\"relay_bytes\":" +
                  std::to_string(metrics.relay_bytes) + "}\n");
   if (required(args, "scenario") == "dcutr_relay_topology" &&
       status != forge::p2p::hole_punch::status::succeeded) {
      throw std::runtime_error{"FORGE relay topology DCUtR did not succeed"};
   }
   return 0;
}

int topology_mode(const std::map<std::string, std::string>& args) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 6}};
   const auto root = std::filesystem::path{required(args, "store-dir")};
   std::filesystem::create_directories(root);

   const auto relay_identity = generate_libp2p_identity();
   const auto source_identity = generate_libp2p_identity();
   const auto destination_identity = generate_libp2p_identity();

   auto relay_options = node_options(root / "relay-store", relay_identity);
   relay_options.capabilities = forge::p2p::capability_set{
       .bits = forge::p2p::capabilities::direct_quic | forge::p2p::capabilities::relay |
               forge::p2p::capabilities::relay_reservation | forge::p2p::capabilities::hole_punching};
   auto source_options = node_options(root / "source-store", source_identity);
   source_options.capabilities = forge::p2p::capability_set{
       .bits = forge::p2p::capabilities::direct_quic | forge::p2p::capabilities::hole_punching};
   auto destination_options = node_options(root / "destination-store", destination_identity);
   destination_options.capabilities = forge::p2p::capability_set{
       .bits = forge::p2p::capabilities::direct_quic | forge::p2p::capabilities::relay_reservation |
               forge::p2p::capabilities::hole_punching};

   auto relay = forge::p2p::node{runtime, std::move(relay_options)};
   auto source = forge::p2p::node{runtime, std::move(source_options)};
   auto destination = forge::p2p::node{runtime, std::move(destination_options)};
   register_echo(destination);

   forge::asio::blocking::run(runtime, relay.async_listen(loopback_quic_endpoint()));
   forge::asio::blocking::run(runtime, source.async_listen(loopback_quic_endpoint()));
   forge::asio::blocking::run(runtime, destination.async_listen(loopback_quic_endpoint()));

   const auto relay_endpoint = relay.local_endpoint();
   const auto source_endpoint = source.local_endpoint();
   const auto destination_endpoint = destination.local_endpoint();
   if (!relay_endpoint || !source_endpoint || !destination_endpoint) {
      throw std::runtime_error{"FORGE topology failed to start all listeners"};
   }

   source.peers().learn_endpoint(relay.local_peer(), *relay_endpoint,
                                 forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                                  forge::p2p::capabilities::relay |
                                                                  forge::p2p::capabilities::relay_reservation});
   destination.peers().learn_endpoint(relay.local_peer(), *relay_endpoint,
                                      forge::p2p::capability_set{.bits = forge::p2p::capabilities::direct_quic |
                                                                       forge::p2p::capabilities::relay |
                                                                       forge::p2p::capabilities::relay_reservation});

   const auto reservation = forge::asio::blocking::run(runtime, destination.async_reserve_relay(relay.local_peer()));
   auto status = forge::p2p::hole_punch::status::failed;
   auto relay_echo = false;
   if (required(args, "scenario") == "relay_echo_topology") {
      auto stream = forge::asio::blocking::run(runtime, source.async_open_protocol_stream(
                                                         destination.local_peer(),
                                                         forge::p2p::protocol_id{.value = std::string{echo_protocol}},
                                                         forge::p2p::node::open_options{
                                                             .allow_relay = true,
                                                             .relay_peer = relay.local_peer(),
                                                             .timeout = 10s,
                                                             .allow_hole_punch = false,
                                                         }));
      const auto payload = std::vector<std::uint8_t>{'r', 'e', 'l', 'a', 'y', '-', 'e', 'c', 'h', 'o'};
      forge::asio::blocking::run(runtime, stream.async_write(wrap_length_delimited(payload)));
      relay_echo = forge::asio::blocking::run(runtime, read_length_delimited(stream, 16 * 1024)) == payload;
      if (!relay_echo) {
         throw std::runtime_error{"FORGE topology relay echo mismatch"};
      }
   } else {
      status = forge::asio::blocking::run(
          runtime, source.async_attempt_hole_punch(destination.local_peer(), relay.local_peer(), 10s));
      relay_echo = status == forge::p2p::hole_punch::status::succeeded;
   }

   const auto source_metrics = source.metrics();
   const auto relay_metrics = relay.metrics();
   const auto destination_metrics = destination.metrics();
   write_file(required(args, "result-file"),
              "{\"implementation\":\"forge\",\"role\":\"topology\",\"scenario\":\"" +
                  json_escape(required(args, "scenario")) + "\","
              "\"status\":\"ok\",\"relay_peer\":\"" +
                  json_escape(relay.local_peer().to_string()) + "\",\"source_peer\":\"" +
                  json_escape(source.local_peer().to_string()) + "\",\"destination_peer\":\"" +
                  json_escape(destination.local_peer().to_string()) + "\",\"relay_addr\":\"" +
                  json_escape(p2p_endpoint_for(*relay_endpoint, relay.local_peer()).to_string()) +
                  "\",\"source_addr\":\"" +
                  json_escape(p2p_endpoint_for(*source_endpoint, source.local_peer()).to_string()) +
                  "\",\"destination_addr\":\"" +
                  json_escape(p2p_endpoint_for(*destination_endpoint, destination.local_peer()).to_string()) +
                  "\",\"reservation_voucher_bytes\":" +
                  std::to_string(reservation.voucher ? reservation.voucher->encode().size() : 0U) +
                  ",\"hole_punch_status\":" + std::to_string(static_cast<int>(status)) +
                  ",\"relay_echo\":" + std::string{relay_echo ? "true" : "false"} +
                  ",\"source_hole_punch_successes\":" + std::to_string(source_metrics.hole_punch_successes) +
                  ",\"relay_bytes\":" + std::to_string(relay_metrics.relay_bytes) +
                  ",\"destination_hole_punch_attempts\":" +
                  std::to_string(destination_metrics.hole_punch_attempts) + "}\n");

   forge::asio::blocking::run(runtime, destination.async_stop());
   forge::asio::blocking::run(runtime, source.async_stop());
   forge::asio::blocking::run(runtime, relay.async_stop());
   return 0;
}

} // namespace

int main(int argc, char** argv) {
   try {
      const auto args = parse_args(argc, argv);
      if (args.at("command") == "listen") {
         return listen_mode(args);
      }
      if (args.at("command") == "destination") {
         return destination_mode(args);
      }
      if (args.at("command") == "dial") {
         return dial_mode(args);
      }
      if (args.at("command") == "dial-relay") {
         return dial_relay_mode(args);
      }
      if (args.at("command") == "topology") {
         return topology_mode(args);
      }
      throw std::runtime_error{"unknown command: " + args.at("command")};
   } catch (const std::exception& error) {
      std::cerr << "forge_interop_fixture: " << error.what() << "\n";
      return 2;
   }
}
