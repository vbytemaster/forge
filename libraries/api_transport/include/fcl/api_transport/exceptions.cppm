export module fcl.api.transport.exceptions;

export import fcl.api.exceptions;

export namespace fcl::api::transport::exceptions {

using cancelled = fcl::api::exceptions::cancelled;
using codec_failed = fcl::api::exceptions::codec_failed;
using deadline_exceeded = fcl::api::exceptions::deadline_exceeded;
using protocol_error = fcl::api::exceptions::protocol_error;
using resource_exhausted = fcl::api::exceptions::resource_exhausted;

} // namespace fcl::api::transport::exceptions
