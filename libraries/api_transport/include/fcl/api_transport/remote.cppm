module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <string>
#include <utility>

export module fcl.api.transport.remote;

export import fcl.api.transport.client;
export import fcl.api.descriptor;
export import fcl.api.error_projection;

import fcl.exceptions;
import fcl.raw.raw;

export namespace fcl::api::transport {

class remote {
 public:
   remote(client value, descriptor contract) : client_(std::move(value)), contract_(std::move(contract)) {}

   template <typename Request, typename Response>
   boost::asio::awaitable<Response> call(api_ref api, std::string method, Request request, call_options value = {}) {
      auto frame = fcl::api::frame{
          .kind = frame_kind::request,
          .id = value.id,
          .api = std::move(api),
          .method = std::move(method),
          .meta = std::move(value.meta),
          .codec = client_.settings().codec,
      };
      fcl::raw::pack(frame.payload, request);

      auto response = co_await client_.async_call(std::move(frame), call_options{.deadline = value.deadline});
      if (response.kind == frame_kind::error) {
         const auto payload = fcl::raw::unpack<error_payload>(response.payload);
         raise_remote_error(payload, find_method(contract_, response.method));
      }
      if (response.kind != frame_kind::response) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "remote API call did not return a response frame",
                             fcl::exceptions::ctx("method", response.method));
      }
      co_return fcl::raw::unpack<Response>(response.payload);
   }

 private:
   client client_;
   descriptor contract_;
};

} // namespace fcl::api::transport
