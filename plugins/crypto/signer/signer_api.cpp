module;

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.crypto.signer.plugin;

import fcl.crypto.asymmetric;
import fcl.plugins.crypto.signer.api;
import fcl.plugins.crypto.signer.types;

#include "details/plugin_impl.hxx"
#include "details/signer_api.hxx"

namespace fcl::plugins::crypto::signer {

plugin::signer_api::signer_api(std::shared_ptr<impl> state) : state_{std::move(state)} {}

boost::asio::awaitable<response> plugin::signer_api::sign(request value) {
   co_return state_->sign(std::move(value));
}

} // namespace fcl::plugins::crypto::signer
