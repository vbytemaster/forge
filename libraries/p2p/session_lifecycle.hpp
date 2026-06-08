#pragma once

#include <map>
#include <memory>

namespace fcl::p2p::detail {

template <typename Id, typename Session>
bool erase_current_session(std::map<Id, std::shared_ptr<Session>>& sessions,
                           const std::shared_ptr<Session>& session) {
   const auto it = sessions.find(session->id);
   if (it == sessions.end() || it->second != session) {
      return false;
   }
   sessions.erase(it);
   return true;
}

template <typename Session>
void cancel_rejected_session(const std::shared_ptr<Session>& session) {
   if (!session) {
      return;
   }
   session->closed = true;
   session->connection.cancel();
}

} // namespace fcl::p2p::detail
