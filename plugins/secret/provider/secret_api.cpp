module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.secret.provider.plugin;

import fcl.crypto.secret_bytes;
import fcl.plugins.secret.provider.api;
import fcl.plugins.secret.provider.types;

#include "details/plugin_impl.hxx"
#include "details/secret_api.hxx"

namespace fcl::plugins::secret::provider {

plugin::secret_api::secret_api(std::shared_ptr<impl> state) : state_{std::move(state)} {}

boost::asio::awaitable<snapshot> plugin::secret_api::status(query value) {
   co_return state_->status(std::move(value));
}

boost::asio::awaitable<get_result> plugin::secret_api::get_bytes(get_request value) {
   co_return state_->get_bytes(std::move(value));
}

boost::asio::awaitable<derive_result> plugin::secret_api::derive_hkdf_sha256(derive_request value) {
   co_return state_->derive_hkdf_sha256(std::move(value));
}

boost::asio::awaitable<aead_encrypt_result> plugin::secret_api::encrypt_aes_gcm(aead_encrypt_request value) {
   co_return state_->encrypt_aes_gcm(std::move(value));
}

boost::asio::awaitable<aead_decrypt_result> plugin::secret_api::decrypt_aes_gcm(aead_decrypt_request value) {
   co_return state_->decrypt_aes_gcm(std::move(value));
}

} // namespace fcl::plugins::secret::provider
