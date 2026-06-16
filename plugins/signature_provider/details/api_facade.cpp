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

#include "details/state.hxx"
#include "details/api_facade.hxx"

namespace fcl::plugins::signature_provider {

plugin::api_impl::api_impl(std::shared_ptr<impl> state) : state_{std::move(state)} {}

boost::asio::awaitable<response> plugin::api_impl::sign(request value) {
   co_return state_->sign(std::move(value));
}

} // namespace fcl::plugins::signature_provider
