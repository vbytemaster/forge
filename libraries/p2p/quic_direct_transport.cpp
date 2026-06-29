module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.p2p.node;

import forge.asio.runtime;
import forge.p2p.endpoint;
import forge.p2p.exceptions;
import forge.p2p.identity;
import forge.multiformats.exceptions;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.multicodec;
import forge.multiformats.multihash;
import forge.multiformats.multibase;
import forge.multiformats.multiaddr;
import forge.quic.connection;
import forge.quic.connector;
import forge.quic.endpoint;
import forge.quic.exceptions;
import forge.quic.listener;
import forge.quic.options;
import forge.quic.security;
import forge.quic.transport;
import forge.transport.limits;
import forge.transport.session;

#include "details/direct_transport.hxx"

namespace forge::p2p::direct {
namespace {

[[nodiscard]] forge::quic::transport_limits quic_limits(const forge::transport::limits& value) noexcept {
   return forge::quic::transport_limits{
       .max_connections = value.max_connections,
       .max_streams_per_connection = value.max_streams_per_connection,
       .max_queued_bytes = value.max_queued_bytes,
       .max_inbound_queued_bytes = value.max_inbound_queued_bytes,
       .max_inbound_queued_packets = value.max_inbound_queued_packets,
       .max_frame_size = value.max_frame_size,
   };
}

[[nodiscard]] forge::quic::endpoint quic_endpoint_for(const forge::p2p::endpoint& value) {
   if (!value.is_direct_quic()) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct QUIC endpoint");
   }
   return forge::quic::from_transport_endpoint(value.transport);
}

[[nodiscard]] forge::p2p::endpoint p2p_endpoint_for(const forge::quic::endpoint& value) {
   return forge::p2p::endpoint{.transport = forge::quic::to_transport_endpoint(value)};
}

[[nodiscard]] std::string listener_key(forge::p2p::endpoint value) {
   value.peer.reset();
   return value.to_string();
}

[[nodiscard]] exceptions::code map_quic_error(forge::quic::exceptions::code kind) noexcept {
   using quic_kind = forge::quic::exceptions::code;
   switch (kind) {
   case quic_kind::invalid_endpoint:
   case quic_kind::invalid_options:
      return exceptions::code::invalid_options;
   case quic_kind::connect_timeout:
   case quic_kind::handshake_timeout:
   case quic_kind::idle_timeout:
      return exceptions::code::timeout;
   case quic_kind::peer_verification_failed:
   case quic_kind::alpn_mismatch:
   case quic_kind::tls_failed:
      return exceptions::code::peer_verification_failed;
   case quic_kind::frame_too_large:
   case quic_kind::malformed_frame:
      return exceptions::code::codec_error;
   case quic_kind::backpressure_rejected:
      return exceptions::code::backpressure_rejected;
   case quic_kind::connection_closed:
   case quic_kind::stream_closed:
   case quic_kind::stream_reset:
      return exceptions::code::closed;
   case quic_kind::canceled:
      return exceptions::code::canceled;
   case quic_kind::dependency_unavailable:
   case quic_kind::internal:
   case quic_kind::unsupported:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_quic_as_p2p(const forge::exceptions::base& error) {
   const auto code = forge::quic::exceptions::code_of(error);
   if (code) {
      FORGE_THROW_CODE(map_quic_error(*code), error.what());
   }
   throw;
}

[[nodiscard]] peer_id insecure_legacy_peer_id(std::span<const std::uint8_t> der) {
   return peer_id::from_bytes(forge::multiformats::multihash::sha2_256(der).encode());
}

[[nodiscard]] peer_id strict_peer_id_from_certificate_der(std::span<const std::uint8_t> der) {
   try {
      return make_peer_id_from_certificate_der(der);
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed,
                          "P2P peer certificate is missing a valid signed libp2p identity extension");
   }
}

[[nodiscard]] peer_id verified_peer_id_for(const forge::quic::connection& connection,
                                           const std::optional<peer_id>& expected, bool insecure_test_mode) {
   if (insecure_test_mode) {
      if (expected) {
         return *expected;
      }
      if (const auto certificate = connection.peer_certificate()) {
         try {
            return make_peer_id_from_certificate_der(certificate->der);
         } catch (const forge::exceptions::base&) {
            // Insecure test mode still accepts legacy certificates without the libp2p extension.
         }
         return insecure_legacy_peer_id(certificate->der);
      }
      return peer_id{.value = "insecure-test-peer"};
   }

   const auto certificate = connection.peer_certificate();
   if (!certificate) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P session has no verified peer certificate");
   }
   const auto remote = strict_peer_id_from_certificate_der(certificate->der);
   if (expected && remote != *expected) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P peer id does not match expected peer");
   }
   return remote;
}

[[nodiscard]] std::optional<peer_id> expected_peer_for(const forge::p2p::endpoint& endpoint,
                                                       const node::connect_options& options) {
   if (options.expected_peer) {
      return options.expected_peer;
   }
   return endpoint.peer;
}

class quic_profile final {
   struct listener_entry {
      std::unique_ptr<forge::quic::listener> value;
      bool active = true;
   };

 public:
   quic_profile(forge::asio::runtime& runtime_value, const node::options& options_value)
       : runtime_(runtime_value), options_(options_value), connector_(runtime_value) {}

   [[nodiscard]] bool supports(const forge::p2p::endpoint& endpoint) const noexcept {
      return endpoint.is_direct_quic();
   }

   [[nodiscard]] bool listening() const noexcept {
      return std::ranges::any_of(listeners_, [](const auto& item) {
         return item.second.active;
      });
   }

   [[nodiscard]] std::vector<forge::p2p::endpoint> local_endpoints() const {
      auto out = std::vector<forge::p2p::endpoint>{};
      out.reserve(listeners_.size());
      for (const auto& [_, listener] : listeners_) {
         if (listener.active) {
            out.push_back(p2p_endpoint_for(listener.value->local_endpoint()));
         }
      }
      return out;
   }

   forge::p2p::endpoint listen(forge::p2p::endpoint endpoint) {
      if (!endpoint.is_direct_quic()) {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct QUIC endpoint");
      }
      const auto requested_key = listener_key(endpoint);
      if (endpoint.transport.port != 0) {
         auto found = listeners_.find(requested_key);
         if (found != listeners_.end() && found->second.active) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P QUIC direct listener endpoint is already active");
         }
      }
      try {
         auto listener = std::make_unique<forge::quic::listener>(runtime_, quic_endpoint_for(endpoint), server_options());
         auto local = p2p_endpoint_for(listener->local_endpoint());
         const auto key = listener_key(local);
         auto found = listeners_.find(key);
         if (found != listeners_.end() && found->second.active) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P QUIC direct listener endpoint is already active");
         }
         listeners_[key] = listener_entry{.value = std::move(listener), .active = true};
         return local;
      } catch (const forge::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   void stop() {
      for (auto& [_, listener] : listeners_) {
         listener.active = false;
         listener.value->stop();
      }
   }

   boost::asio::awaitable<connection> async_connect(forge::p2p::endpoint endpoint,
                                                    const node::connect_options& options) {
      try {
         const auto expected_peer = expected_peer_for(endpoint, options);
         auto quic = co_await connector_.async_connect(quic_endpoint_for(endpoint),
                                                       client_options(expected_peer, options.timeout));
         const auto remote = verified_peer_id_for(quic, expected_peer, options_.allow_insecure_test_mode);
         auto local_endpoint = p2p_endpoint_for(quic.local_endpoint());
         auto remote_endpoint = p2p_endpoint_for(quic.remote_endpoint());
         co_return connection{
             .peer = remote,
             .session = forge::quic::as_transport_session(std::move(quic)),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const forge::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<connection> async_accept(forge::p2p::endpoint endpoint) {
      try {
         auto found = listeners_.find(listener_key(std::move(endpoint)));
         if (found == listeners_.end() || !found->second.active) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P QUIC direct listener is not active");
         }
         auto quic = co_await found->second.value->async_accept();
         const auto remote = verified_peer_id_for(quic, std::nullopt, options_.allow_insecure_test_mode);
         auto local_endpoint = p2p_endpoint_for(quic.local_endpoint());
         auto remote_endpoint = p2p_endpoint_for(quic.remote_endpoint());
         co_return connection{
             .peer = remote,
             .session = forge::quic::as_transport_session(std::move(quic)),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const forge::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

 private:
   [[nodiscard]] forge::quic::security_options peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options_.allow_insecure_test_mode) {
         auto security = forge::quic::security_options{.verify_peer = true};
         security.verifier = [](const forge::quic::peer_certificate&) { return true; };
         return security;
      }
      auto security = forge::quic::security_options{.verify_peer = true};
      security.verifier = [expected = std::move(expected)](const forge::quic::peer_certificate& certificate) {
         try {
            const auto remote = make_peer_id_from_certificate_der(certificate.der);
            if (expected) {
               return remote == *expected;
            }
            return valid_peer_id(remote);
         } catch (const forge::exceptions::base&) {
            return false;
         }
      };
      return security;
   }

   [[nodiscard]] forge::quic::client_options client_options(std::optional<peer_id> expected,
                                                          std::chrono::milliseconds timeout) const {
      return forge::quic::client_options{
          .alpn = "libp2p",
          .connect_timeout = timeout,
          .handshake_timeout = timeout,
          .limits = quic_limits(options_.transport_limits),
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options_.certificate_pem,
          .private_key_pem = options_.private_key_pem,
      };
   }

   [[nodiscard]] forge::quic::server_options server_options() const {
      return forge::quic::server_options{
          .alpn = "libp2p",
          .limits = quic_limits(options_.transport_limits),
          .security = peer_verifier(),
          .certificate_pem = options_.certificate_pem,
          .private_key_pem = options_.private_key_pem,
      };
   }

   forge::asio::runtime& runtime_;
   const node::options& options_;
   forge::quic::connector connector_;
   std::map<std::string, listener_entry> listeners_;
};

} // namespace

void register_quic_profile(registry& value, forge::asio::runtime& runtime, const node::options& options) {
   auto owned = std::make_shared<quic_profile>(runtime, options);
   value.add(profile{
       .supports = [owned](const forge::p2p::endpoint& endpoint) { return owned->supports(endpoint); },
       .listening = [owned] { return owned->listening(); },
       .local_endpoints = [owned] { return owned->local_endpoints(); },
       .listen = [owned](forge::p2p::endpoint endpoint) { return owned->listen(std::move(endpoint)); },
       .stop = [owned] { owned->stop(); },
       .async_connect =
           [owned](forge::p2p::endpoint endpoint, const node::connect_options& options) {
              return owned->async_connect(std::move(endpoint), options);
           },
       .async_accept = [owned](forge::p2p::endpoint endpoint) { return owned->async_accept(std::move(endpoint)); },
   });
}

} // namespace forge::p2p::direct
