module;

#include <boost/asio/awaitable.hpp>

#include <vector>

export module forge.chain.api.submission_client;

export import forge.chain.api.limits;
export import forge.chain.api.submission;

import forge.api.core.handle;

export namespace forge::chain::api {

// Submission acknowledges transport acceptance only. Finality remains a
// separate verified_client operation over the returned transaction id.
class submission_client {
 public:
   explicit submission_client(forge::api::core::handle<submission> service, protocol::service_limits limits = {});

   boost::asio::awaitable<protocol::transaction_submit_response> submit(protocol::transaction_submit_request request);
   boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
   submit_batch(protocol::transaction_submit_batch_request request);

 private:
   forge::api::core::handle<submission> service_;
   protocol::service_limits limits_;
};

} // namespace forge::chain::api
