module;

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.crypto.signer.plugin;

import forge.crypto.asymmetric;
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.types;

#include "details/plugin_impl.hxx"
#include "details/signer_api.hxx"

namespace forge::plugins::crypto::signer {

plugin::signer_api::signer_api(std::shared_ptr<impl> state) : state_{std::move(state)} {}

boost::asio::awaitable<response> plugin::signer_api::sign(request value) {
   co_return state_->sign(std::move(value));
}

} // namespace forge::plugins::crypto::signer
