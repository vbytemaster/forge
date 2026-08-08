# Forge API Live Streaming v2 Donor Baseline

## Donors Reviewed

The live streaming design follows protocol mechanics from:

- gRPC flow control, where a write waits when receiver capacity is exhausted
  and capacity is returned as the application consumes messages;
- HTTP/2 RFC 9113, which separates per-stream and per-connection credit and
  keeps control frames outside the data window;
- QUIC RFC 9000, which advertises monotonic absolute stream and connection
  limits through `MAX_STREAM_DATA` and `MAX_DATA`;
- gRPC C++ client and server streaming reactors, which expose different
  client-facing and handler-facing views over one RPC contract.

Primary references:

- <https://grpc.io/docs/guides/flow-control/>
- <https://grpc.github.io/grpc/cpp/support_2server__callback_8h.html>
- <https://grpc.github.io/grpc/cpp/support_2client__callback_8h_source.html>
- <https://www.rfc-editor.org/rfc/rfc9113.html#section-5.2>
- <https://www.rfc-editor.org/rfc/rfc9000.html#section-4.1>

## Accepted Patterns

- receiver-driven per-call item and byte credit;
- a separate aggregate connection memory budget;
- monotonic absolute credit limits rather than replay-sensitive increments;
- independent read and write directions with explicit half-close;
- client-oriented call handles generated from server-oriented method shapes;
- one terminal outcome that wakes every blocked operation;
- control traffic that cannot be trapped behind flow-controlled data;
- explicit version and capability negotiation before application requests.

## Rejected Patterns

- materializing a live stream as `std::vector`;
- stopping the shared connection reader when one call queue is full;
- relying only on the underlying QUIC or Yamux window after multiple API calls
  have been multiplexed into one transport stream;
- treating `stream_end` as termination of both duplex directions;
- detached producer tasks without an owning call or connection state;
- emulating HTTP/1.1 bidirectional streaming by buffering either direction;
- accepting an old peer and waiting indefinitely for credit it cannot send.

## Forge Mapping

- API Core owns endpoint types, call handles, descriptors, wire records and the
  transport-neutral call state machine.
- API Stream and API Transport own the v2 hello exchange, multiplexed reader
  and fair writer loops over an established byte stream.
- P2P and QUIC reuse that shared stream implementation.
- WebSocket maps one API frame to one WebSocket binary message while retaining
  the same call state and credit rules.
- HTTP/1.1 maps server streams to chunked responses and client streams to
  streamed request bodies. Its native body backpressure replaces wire window
  frames; bidirectional methods are rejected before I/O.
