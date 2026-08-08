module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

module forge.chain.api.submission_client;

import forge.api.core.exceptions;
import forge.chain.api.exceptions;
import forge.asio.exceptions;

namespace forge::chain::api {
namespace {

submission& require_service(const forge::api::core::handle<submission>& value) {
   if (!value) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain transaction submission service is unavailable");
   }
   return *value.shared();
}

void verify_acknowledgement(const protocol::transaction_submit_response& response,
                            const protocol::transaction_id& expected, const char* message) {
   if (response.id != expected || (response.trace && response.trace->id != expected)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof, message);
   }
}

template <typename Response, typename Operation>
boost::asio::awaitable<Response> invoke_service(const char* method, Operation&& operation) {
   try {
      co_return co_await std::forward<Operation>(operation)();
   } catch (const forge::asio::exceptions::canceled&) {
      throw;
   } catch (const forge::api::core::exceptions::cancelled&) {
      FORGE_THROW_EXCEPTION(forge::asio::exceptions::canceled, "chain API request was canceled",
                            forge::exceptions::ctx("method", method));
   } catch (const forge::api::core::exceptions::deadline_exceeded&) {
      FORGE_THROW_EXCEPTION(exceptions::deadline_exceeded, "chain API request deadline expired",
                            forge::exceptions::ctx("method", method));
   } catch (const forge::exceptions::base& error) {
      if (std::string_view{error.code().category().name()} == "forge.chain.api") {
         throw;
      }
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API service failed",
                            forge::exceptions::ctx("method", method), forge::exceptions::ctx("reason", error.what()));
   } catch (const boost::system::system_error& error) {
      if (error.code() == boost::asio::error::operation_aborted) {
         FORGE_THROW_EXCEPTION(forge::asio::exceptions::canceled, "chain API request was canceled",
                               forge::exceptions::ctx("method", method));
      }
      if (error.code() == boost::asio::error::timed_out) {
         FORGE_THROW_EXCEPTION(exceptions::deadline_exceeded, "chain API request deadline expired",
                               forge::exceptions::ctx("method", method));
      }
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API service failed",
                            forge::exceptions::ctx("method", method), forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API service failed",
                            forge::exceptions::ctx("method", method), forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API service failed",
                            forge::exceptions::ctx("method", method));
   }
}

} // namespace

submission_client::submission_client(forge::api::core::handle<submission> service, protocol::service_limits limits)
    : service_{std::move(service)}, limits_{limits} {}

boost::asio::awaitable<protocol::transaction_submit_response>
submission_client::submit(protocol::transaction_submit_request request) {
   auto& service = require_service(service_);
   require_request_within_limits(request, limits_);
   const auto expected = request.transaction.id();
   auto response = co_await invoke_service<protocol::transaction_submit_response>(
       "submission.submit", [&] { return service.submit(std::move(request)); });
   require_response_within_limits(response, limits_);
   verify_acknowledgement(response, expected,
                          "chain API submit acknowledgement does not match the submitted transaction");
   co_return response;
}

boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
submission_client::submit_batch(protocol::transaction_submit_batch_request request) {
   auto& service = require_service(service_);
   require_request_within_limits(request, limits_);
   auto expected = std::vector<protocol::transaction_id>{};
   expected.reserve(request.transactions.size());
   for (const auto& transaction : request.transactions) {
      expected.push_back(transaction.transaction.id());
   }

   auto responses = co_await invoke_service<std::vector<protocol::transaction_submit_response>>(
       "submission.submit_batch", [&] { return service.submit_batch(std::move(request)); });
   require_transaction_batch_response_within_limits(responses, expected.size(), limits_);
   if (responses.size() != expected.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                            "chain API submit acknowledgement count does not match the request");
   }
   for (auto index = std::size_t{}; index < responses.size(); ++index) {
      verify_acknowledgement(responses[index], expected[index],
                             "chain API submit acknowledgement does not match transaction order");
   }
   co_return responses;
}

} // namespace forge::chain::api
