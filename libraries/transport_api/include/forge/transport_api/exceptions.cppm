export module forge.transport.api.exceptions;

export import forge.api.exceptions;

export namespace forge::transport::api::exceptions {

using cancelled = forge::api::exceptions::cancelled;
using codec_failed = forge::api::exceptions::codec_failed;
using deadline_exceeded = forge::api::exceptions::deadline_exceeded;
using protocol_error = forge::api::exceptions::protocol_error;
using resource_exhausted = forge::api::exceptions::resource_exhausted;

} // namespace forge::transport::api::exceptions
