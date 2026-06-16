module;

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.signature_provider.plugin;

import fcl.crypto.asymmetric;
import fcl.plugins.signature_provider.api;
import fcl.plugins.signature_provider.types;

#include "details/plugin_impl.hxx"
#include "details/signing_api.hxx"

namespace fcl::plugins::signature_provider {

plugin::signing_api::signing_api(std::shared_ptr<impl> state) : state_{std::move(state)} {}

boost::asio::awaitable<response> plugin::signing_api::sign(request value) {
   co_return state_->sign(std::move(value));
}

} // namespace fcl::plugins::signature_provider
