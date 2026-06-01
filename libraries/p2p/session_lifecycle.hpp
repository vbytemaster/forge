#pragma once

#include <map>
#include <memory>

namespace fcl::p2p::detail {

template <typename Peer, typename Session>
bool erase_current_session(std::map<Peer, std::shared_ptr<Session>>& sessions,
                           const std::shared_ptr<Session>& session) {
   const auto it = sessions.find(session->info.remote_peer);
   if (it == sessions.end() || it->second != session) {
      return false;
   }
   sessions.erase(it);
   return true;
}

} // namespace fcl::p2p::detail
