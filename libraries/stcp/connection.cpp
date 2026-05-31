module;

#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

module fcl.stcp.connection;

import fcl.crypto.x509;
import fcl.transport.stream;

namespace fcl::stcp {
namespace {

namespace asio = boost::asio;
using asio_tcp = boost::asio::ip::tcp;
using native_stream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

struct x509_deleter {
   void operator()(X509* value) const noexcept {
      X509_free(value);
   }
};

using x509_ptr = std::unique_ptr<X509, x509_deleter>;

[[nodiscard]] std::int64_t next_stream_id() noexcept {
   static auto next = std::atomic<std::int64_t>{1};
   return next.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void throw_invalid_options(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[noreturn]] void throw_io_error(std::string message, const boost::system::error_code& error) {
   FCL_THROW_EXCEPTION(exceptions::io_error, std::move(message), fcl::exception::ctx("reason", error.message()));
}

[[noreturn]] void throw_handshake_failed(std::string message, const boost::system::error_code& error) {
   if (error == boost::asio::error::operation_aborted) {
      FCL_THROW_EXCEPTION(exceptions::canceled, "stcp handshake canceled",
                          fcl::exception::ctx("reason", error.message()));
   }
   FCL_THROW_EXCEPTION(exceptions::handshake_failed, std::move(message),
                       fcl::exception::ctx("reason", error.message()));
}

[[noreturn]] void throw_verification_failed(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::verification_failed, std::move(message));
}

[[noreturn]] void throw_read_write_error(const boost::system::error_code& error) {
   if (error == boost::asio::error::operation_aborted) {
      FCL_THROW_EXCEPTION(exceptions::canceled, "stcp connection operation canceled",
                          fcl::exception::ctx("reason", error.message()));
   }
   if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset ||
       error == boost::asio::error::broken_pipe || error == boost::asio::ssl::error::stream_truncated) {
      FCL_THROW_EXCEPTION(exceptions::closed, "stcp connection closed", fcl::exception::ctx("reason", error.message()));
   }
   throw_io_error("stcp connection I/O failed", error);
}

[[nodiscard]] std::string normalize_fingerprint(std::string_view value) {
   auto out = std::string{};
   out.reserve(value.size());
   for (const auto ch : value) {
      if (ch == ':' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
         continue;
      }
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
   }
   return out;
}

[[nodiscard]] std::vector<unsigned char> encode_alpn(const std::vector<std::string>& protocols) {
   auto out = std::vector<unsigned char>{};
   for (const auto& protocol : protocols) {
      if (protocol.empty() || protocol.size() > 255) {
         throw_invalid_options("stcp ALPN protocol length must be 1..255 bytes");
      }
      out.push_back(static_cast<unsigned char>(protocol.size()));
      out.insert(out.end(), protocol.begin(), protocol.end());
   }
   return out;
}

int select_alpn(SSL*, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen,
                void* arg) {
   const auto* supported = static_cast<const std::vector<std::string>*>(arg);
   auto offset = unsigned{0};
   while (offset < inlen) {
      const auto length = static_cast<unsigned>(in[offset]);
      ++offset;
      if (length == 0 || offset + length > inlen) {
         return SSL_TLSEXT_ERR_NOACK;
      }
      const auto value = std::string_view{reinterpret_cast<const char*>(in + offset), length};
      if (std::find(supported->begin(), supported->end(), value) != supported->end()) {
         *out = in + offset;
         *outlen = static_cast<unsigned char>(length);
         return SSL_TLSEXT_ERR_OK;
      }
      offset += length;
   }
   return SSL_TLSEXT_ERR_NOACK;
}

void validate_identity_pair(std::string_view certificate_pem, std::string_view private_key_pem) {
   if (certificate_pem.empty() != private_key_pem.empty()) {
      throw_invalid_options("stcp certificate and private key must be configured together");
   }
}

void validate_common(std::size_t read_chunk_size, const std::vector<std::string>& alpn_protocols) {
   if (read_chunk_size == 0) {
      throw_invalid_options("stcp read_chunk_size must be greater than zero");
   }
   (void)encode_alpn(alpn_protocols);
}

void load_identity(asio::ssl::context& context, std::string_view certificate_pem, std::string_view private_key_pem) {
   if (certificate_pem.empty() && private_key_pem.empty()) {
      return;
   }
   try {
      context.use_certificate_chain(asio::buffer(certificate_pem.data(), certificate_pem.size()));
      context.use_private_key(asio::buffer(private_key_pem.data(), private_key_pem.size()), asio::ssl::context::pem);
   } catch (const boost::system::system_error& error) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "failed to load stcp certificate or private key",
                          fcl::exception::ctx("reason", error.code().message()));
   }
}

void load_trust(asio::ssl::context& context, const security_options& security) {
   if (!security.trusted_ca_pem.empty()) {
      try {
         context.add_certificate_authority(asio::buffer(security.trusted_ca_pem.data(), security.trusted_ca_pem.size()));
      } catch (const boost::system::system_error& error) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "failed to load stcp trusted CA",
                             fcl::exception::ctx("reason", error.code().message()));
      }
      return;
   }
   if (security.verify_peer) {
      auto error = boost::system::error_code{};
      context.set_default_verify_paths(error);
      if (error) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "failed to load default TLS verify paths",
                             fcl::exception::ctx("reason", error.message()));
      }
   }
}

[[nodiscard]] std::shared_ptr<asio::ssl::context> make_client_context(const client_options& options) {
   validate_identity_pair(options.certificate_pem, options.private_key_pem);
   validate_common(options.read_chunk_size, options.alpn_protocols);
   auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
   context->set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                        asio::ssl::context::no_sslv3);
   load_identity(*context, options.certificate_pem, options.private_key_pem);
   load_trust(*context, options.security);
   context->set_verify_mode(options.security.verify_peer ? asio::ssl::verify_peer : asio::ssl::verify_none);
   return context;
}

[[nodiscard]] std::shared_ptr<asio::ssl::context> make_server_context(const server_options& options) {
   if (options.certificate_pem.empty() || options.private_key_pem.empty()) {
      throw_invalid_options("stcp server requires certificate and private key");
   }
   validate_common(options.read_chunk_size, options.alpn_protocols);
   auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
   context->set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                        asio::ssl::context::no_sslv3);
   load_identity(*context, options.certificate_pem, options.private_key_pem);
   load_trust(*context, options.security);
   auto mode = options.security.verify_peer ? asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert
                                            : asio::ssl::verify_none;
   context->set_verify_mode(mode);
   return context;
}

[[nodiscard]] transport::endpoint from_asio_endpoint(const asio_tcp::endpoint& endpoint) {
   const auto address = endpoint.address();
   return transport::endpoint{.host_type = address.is_v6() ? transport::endpoint::host_kind::ip6
                                                           : transport::endpoint::host_kind::ip4,
                              .protocol = transport::endpoint::protocol_kind::tcp,
                              .host = address.to_string(),
                              .port = endpoint.port()};
}

[[nodiscard]] std::vector<std::uint8_t> certificate_der(X509* certificate) {
   const auto length = i2d_X509(certificate, nullptr);
   if (length <= 0) {
      FCL_THROW_EXCEPTION(exceptions::verification_failed, "failed to size peer certificate DER");
   }
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_X509(certificate, &cursor) != length) {
      FCL_THROW_EXCEPTION(exceptions::verification_failed, "failed to write peer certificate DER");
   }
   return out;
}

[[nodiscard]] std::optional<peer_certificate> read_peer_certificate(native_stream& stream) {
   auto certificate = x509_ptr{SSL_get1_peer_certificate(stream.native_handle())};
   if (!certificate) {
      return std::nullopt;
   }
   auto der = certificate_der(certificate.get());
   auto parsed = crypto::x509::certificate::from_der(der);
   return peer_certificate{.der = std::move(der), .sha256_fingerprint = parsed.fingerprint_sha256_text()};
}

void verify_host_name(const peer_certificate& certificate, std::string_view host) {
   if (host.empty()) {
      return;
   }
   const auto* cursor = certificate.der.data();
   auto parsed = x509_ptr{d2i_X509(nullptr, &cursor, static_cast<long>(certificate.der.size()))};
   if (!parsed) {
      throw_verification_failed("failed to parse peer certificate for host verification");
   }

   auto address_error = boost::system::error_code{};
   (void)boost::asio::ip::make_address(std::string{host}, address_error);
   const auto ok = address_error ? X509_check_host(parsed.get(), host.data(), host.size(), 0, nullptr)
                                 : X509_check_ip_asc(parsed.get(), std::string{host}.c_str(), 0);
   if (ok != 1) {
      FCL_THROW_EXCEPTION(exceptions::verification_failed, "stcp peer certificate host mismatch",
                          fcl::exception::ctx("host", std::string{host}));
   }
}

void verify_peer(native_stream& stream, const security_options& security, std::string_view expected_host) {
   if (!security.verify_peer && !security.expected_sha256_fingerprint && !security.verifier) {
      return;
   }
   const auto certificate = read_peer_certificate(stream);
   if (!certificate) {
      throw_verification_failed("stcp peer did not present certificate");
   }
   if (security.verify_peer) {
      verify_host_name(*certificate, expected_host);
   }
   if (security.expected_sha256_fingerprint) {
      const auto actual = normalize_fingerprint(certificate->sha256_fingerprint);
      const auto expected = normalize_fingerprint(*security.expected_sha256_fingerprint);
      if (actual != expected) {
         FCL_THROW_EXCEPTION(exceptions::verification_failed, "stcp peer certificate fingerprint mismatch",
                             fcl::exception::ctx("actual", actual));
      }
   }
   if (security.verifier && !security.verifier(*certificate)) {
      throw_verification_failed("stcp peer verifier rejected certificate");
   }
}

void configure_client_stream(native_stream& stream, const client_options& options, std::string_view remote_host) {
   const auto host = options.server_name.empty() ? std::string{remote_host} : options.server_name;
   if (!host.empty()) {
      if (SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) != 1) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "failed to configure stcp SNI");
      }
   }
   const auto alpn = encode_alpn(options.alpn_protocols);
   if (!alpn.empty() && SSL_set_alpn_protos(stream.native_handle(), alpn.data(), static_cast<unsigned>(alpn.size())) != 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "failed to configure stcp ALPN");
   }
}

void configure_server_context(asio::ssl::context& context, const server_options& options) {
   if (!options.alpn_protocols.empty()) {
      SSL_CTX_set_alpn_select_cb(context.native_handle(), select_alpn,
                                 const_cast<std::vector<std::string>*>(&options.alpn_protocols));
   }
}

[[nodiscard]] std::string selected_alpn(native_stream& stream) {
   const auto* data = static_cast<const unsigned char*>(nullptr);
   auto length = unsigned{};
   SSL_get0_alpn_selected(stream.native_handle(), &data, &length);
   if (data == nullptr || length == 0) {
      return {};
   }
   return std::string{reinterpret_cast<const char*>(data), length};
}

class stream_model final : public transport::detail::stream_concept {
 public:
   stream_model(std::shared_ptr<native_stream> stream, std::shared_ptr<asio::ssl::context> context,
                std::size_t read_chunk_size, std::int64_t id)
       : stream_(std::move(stream)), context_(std::move(context)), read_chunk_size_(read_chunk_size), id_(id) {}

   [[nodiscard]] bool valid() const noexcept override {
      return stream_ && stream_->lowest_layer().is_open();
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp stream");
      }
      auto error = boost::system::error_code{};
      co_await boost::asio::async_write(*stream_, boost::asio::buffer(bytes),
                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp stream");
      }
      auto out = std::vector<std::uint8_t>(read_chunk_size_);
      auto error = boost::system::error_code{};
      const auto size = co_await stream_->async_read_some(boost::asio::buffer(out),
                                                          boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
      out.resize(size);
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      if (!stream_) {
         co_return;
      }
      auto ignored = boost::system::error_code{};
      stream_->lowest_layer().shutdown(asio_tcp::socket::shutdown_both, ignored);
      stream_->lowest_layer().close(ignored);
      co_return;
   }

 private:
   std::shared_ptr<native_stream> stream_;
   std::shared_ptr<asio::ssl::context> context_;
   std::size_t read_chunk_size_ = 64 * 1024;
   std::int64_t id_ = -1;
};

} // namespace

struct connection::impl final {
   impl(native_stream stream_value, std::shared_ptr<asio::ssl::context> context_value, std::size_t read_chunk_size_value)
       : stream(std::make_shared<native_stream>(std::move(stream_value))), context(std::move(context_value)),
         read_chunk_size(read_chunk_size_value), id(next_stream_id()) {}

   [[nodiscard]] bool valid() const noexcept {
      return stream && stream->lowest_layer().is_open();
   }

   [[nodiscard]] transport::endpoint local_endpoint() const {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
      }
      auto error = boost::system::error_code{};
      const auto endpoint = stream->lowest_layer().local_endpoint(error);
      if (error) {
         throw_io_error("failed to read stcp local endpoint", error);
      }
      return from_asio_endpoint(endpoint);
   }

   [[nodiscard]] transport::endpoint remote_endpoint() const {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
      }
      auto error = boost::system::error_code{};
      const auto endpoint = stream->lowest_layer().remote_endpoint(error);
      if (error) {
         throw_io_error("failed to read stcp remote endpoint", error);
      }
      return from_asio_endpoint(endpoint);
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
      }
      auto error = boost::system::error_code{};
      co_await boost::asio::async_write(*stream, boost::asio::buffer(bytes),
                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
      }
      auto out = std::vector<std::uint8_t>(read_chunk_size);
      auto error = boost::system::error_code{};
      const auto size = co_await stream->async_read_some(boost::asio::buffer(out),
                                                         boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
      out.resize(size);
      co_return out;
   }

   boost::asio::awaitable<void> async_close() {
      if (!stream) {
         co_return;
      }
      auto ignored = boost::system::error_code{};
      stream->lowest_layer().shutdown(asio_tcp::socket::shutdown_both, ignored);
      stream->lowest_layer().close(ignored);
      co_return;
   }

   [[nodiscard]] transport::stream_connection into_transport_stream() {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
      }
      auto local = local_endpoint();
      auto remote = remote_endpoint();
      auto transport_stream = transport::detail::stream_access::make(
          std::make_shared<stream_model>(stream, context, read_chunk_size, id));
      stream.reset();
      return transport::stream_connection{.local_endpoint = std::move(local),
                                          .remote_endpoint = std::move(remote),
                                          .stream = std::move(transport_stream)};
   }

   std::shared_ptr<native_stream> stream;
   std::shared_ptr<asio::ssl::context> context;
   std::size_t read_chunk_size = 64 * 1024;
   std::int64_t id = -1;
};

connection::connection() = default;
connection::connection(native_token, native_stream stream, std::shared_ptr<boost::asio::ssl::context> context,
                       std::size_t read_chunk_size)
    : impl_(std::make_shared<impl>(std::move(stream), std::move(context), read_chunk_size)) {}
connection::~connection() = default;
connection::connection(connection&&) noexcept = default;
connection& connection::operator=(connection&&) noexcept = default;

bool connection::valid() const noexcept {
   return impl_ && impl_->valid();
}

transport::endpoint connection::local_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   return impl_->local_endpoint();
}

transport::endpoint connection::remote_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   return impl_->remote_endpoint();
}

std::optional<peer_certificate> connection::peer_certificate() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   return read_peer_certificate(*impl_->stream);
}

std::string connection::selected_alpn() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   return ::fcl::stcp::selected_alpn(*impl_->stream);
}

boost::asio::awaitable<void> connection::async_write(std::span<const std::uint8_t> bytes) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   co_await impl_->async_write(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> connection::async_read() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   co_return co_await impl_->async_read();
}

boost::asio::awaitable<void> connection::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->async_close();
}

transport::stream_connection connection::into_transport_stream() && {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connection");
   }
   auto out = impl_->into_transport_stream();
   impl_.reset();
   return out;
}

boost::asio::awaitable<connection> async_upgrade_client(tcp::connection source, client_options options) {
   if (!source.valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid source tcp connection");
   }
   const auto remote = source.remote_endpoint();
   auto context = make_client_context(options);
   auto stream = native_stream{std::move(source).release_socket(), *context};
   configure_client_stream(stream, options, remote.host);

   auto error = boost::system::error_code{};
   co_await stream.async_handshake(asio::ssl::stream_base::client,
                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
   if (error) {
      const auto verify_result = SSL_get_verify_result(stream.native_handle());
      if (options.security.verify_peer && verify_result != X509_V_OK) {
         FCL_THROW_EXCEPTION(exceptions::verification_failed, "stcp server certificate verification failed",
                             fcl::exception::ctx("reason", X509_verify_cert_error_string(verify_result)));
      }
      throw_handshake_failed("stcp client handshake failed", error);
   }
   const auto expected_host = options.server_name.empty() ? remote.host : options.server_name;
   verify_peer(stream, options.security, expected_host);
   co_return connection{connection::native_token{}, std::move(stream), std::move(context), options.read_chunk_size};
}

boost::asio::awaitable<connection> async_upgrade_server(tcp::connection source, server_options options) {
   if (!source.valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid source tcp connection");
   }
   auto context = make_server_context(options);
   configure_server_context(*context, options);
   auto stream = native_stream{std::move(source).release_socket(), *context};
   auto error = boost::system::error_code{};
   co_await stream.async_handshake(asio::ssl::stream_base::server,
                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
   if (error) {
      throw_handshake_failed("stcp server handshake failed", error);
   }
   verify_peer(stream, options.security, {});
   co_return connection{connection::native_token{}, std::move(stream), std::move(context), options.read_chunk_size};
}

} // namespace fcl::stcp
