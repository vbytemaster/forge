package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	libp2p "github.com/libp2p/go-libp2p"
	"github.com/libp2p/go-libp2p/core/host"
	"github.com/libp2p/go-libp2p/core/network"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/core/protocol"
	relayclient "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/client"
	relayv2 "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay"
	"github.com/libp2p/go-libp2p/p2p/protocol/holepunch"
	"github.com/libp2p/go-libp2p/p2p/protocol/identify"
	"github.com/libp2p/go-libp2p/p2p/protocol/ping"
	quic "github.com/libp2p/go-libp2p/p2p/transport/quic"
	ma "github.com/multiformats/go-multiaddr"
)

const echoProtocol = protocol.ID("/fcl/interop/relay-echo/1")

type options struct {
	command     string
	scenario    string
	peerID      string
	addr        string
	relayAddr   string
	relayPeerID string
	readyFile   string
	stopFile    string
	resultFile  string
}

func parseArgs() (options, error) {
	if len(os.Args) < 2 {
		return options{}, fmt.Errorf("missing command")
	}
	out := options{command: os.Args[1]}
	for i := 2; i < len(os.Args); i++ {
		key := os.Args[i]
		if i+1 >= len(os.Args) {
			return options{}, fmt.Errorf("missing value for %s", key)
		}
		value := os.Args[i+1]
		i++
		switch key {
		case "--scenario":
			out.scenario = value
		case "--peer-id":
			out.peerID = value
		case "--addr":
			out.addr = value
		case "--relay-addr":
			out.relayAddr = value
		case "--relay-peer-id":
			out.relayPeerID = value
		case "--ready-file":
			out.readyFile = value
		case "--stop-file":
			out.stopFile = value
		case "--result-file":
			out.resultFile = value
		case "--store-dir", "--features":
			// Accepted for CLI parity with the FCL fixture.
		default:
			return options{}, fmt.Errorf("unknown argument %s", key)
		}
	}
	return out, nil
}

func writeJSON(path string, value any) error {
	data, err := json.Marshal(value)
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0o644)
}

func writeFrame(w io.Writer, payload []byte) error {
	var prefix [binary.MaxVarintLen64]byte
	size := binary.PutUvarint(prefix[:], uint64(len(payload)))
	if _, err := w.Write(prefix[:size]); err != nil {
		return err
	}
	_, err := w.Write(payload)
	return err
}

func readFrame(r *bufio.Reader) ([]byte, error) {
	size, err := binary.ReadUvarint(r)
	if err != nil {
		return nil, err
	}
	if size == 0 || size > 16*1024 {
		return nil, fmt.Errorf("invalid frame size %d", size)
	}
	payload := make([]byte, size)
	if _, err := io.ReadFull(r, payload); err != nil {
		return nil, err
	}
	return payload, nil
}

func installEchoHandler(h host.Host) {
	h.SetStreamHandler(echoProtocol, func(s network.Stream) {
		defer s.Close()
		payload, err := readFrame(bufio.NewReader(s))
		if err != nil {
			_ = s.Reset()
			return
		}
		if err := writeFrame(s, payload); err != nil {
			_ = s.Reset()
		}
	})
}

type fixtureHost struct {
	host.Host
	holePunch *holepunch.Service
}

func (h *fixtureHost) Close() error {
	if h.holePunch != nil {
		_ = h.holePunch.Close()
	}
	return h.Host.Close()
}

func newHost() (*fixtureHost, error) {
	h, err := libp2p.New(
		libp2p.NoTransports,
		libp2p.Transport(quic.NewTransport),
		libp2p.ListenAddrStrings("/ip4/127.0.0.1/udp/0/quic-v1"),
		libp2p.ForceReachabilityPublic(),
		libp2p.EnableAutoNATv2(),
		libp2p.EnableRelay(),
	)
	if err != nil {
		return nil, err
	}
	installEchoHandler(h)
	if _, err := relayv2.New(h); err != nil {
		h.Close()
		return nil, err
	}
	type idServiceHost interface {
		IDService() identify.IDService
	}
	identityHost, ok := h.(idServiceHost)
	if !ok {
		h.Close()
		return nil, fmt.Errorf("host does not expose identify service")
	}
	holePunchService, err := holepunch.NewService(h, identityHost.IDService(), h.Addrs)
	if err != nil {
		h.Close()
		return nil, err
	}
	return &fixtureHost{Host: h, holePunch: holePunchService}, nil
}

func listen(opts options) error {
	h, err := newHost()
	if err != nil {
		return err
	}
	defer h.Close()

	out := make([]string, 0, len(h.Addrs()))
	for _, addr := range h.Addrs() {
		out = append(out, addr.String()+"/p2p/"+h.ID().String())
	}
	protocols := make([]string, 0, len(h.Mux().Protocols()))
	for _, protocolID := range h.Mux().Protocols() {
		protocols = append(protocols, string(protocolID))
	}
	if err := writeJSON(opts.readyFile, map[string]any{
		"implementation": "go",
		"role":           "listener",
		"peer_id":        h.ID().String(),
		"listen_addrs":   out,
		"protocols":      protocols,
		"status":         "ready",
	}); err != nil {
		return err
	}
	for {
		if _, err := os.Stat(opts.stopFile); err == nil {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func destination(opts options) error {
	h, err := newHost()
	if err != nil {
		return err
	}
	defer h.Close()

	relayAddr, err := ma.NewMultiaddr(opts.relayAddr)
	if err != nil {
		return err
	}
	relayInfo, err := peer.AddrInfoFromP2pAddr(relayAddr)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := h.Connect(ctx, *relayInfo); err != nil {
		return fmt.Errorf("connect relay failed: %w", err)
	}
	reservation, err := relayclient.Reserve(ctx, h, *relayInfo)
	if err != nil {
		return fmt.Errorf("reserve relay failed: %w", err)
	}
	listenAddrs := make([]string, 0, len(h.Addrs()))
	for _, addr := range h.Addrs() {
		listenAddrs = append(listenAddrs, addr.String()+"/p2p/"+h.ID().String())
	}
	relayAddrs := make([]string, 0, len(reservation.Addrs))
	for _, addr := range reservation.Addrs {
		relayAddrs = append(relayAddrs, addr.String())
	}
	voucherPayloadBytes := 0
	voucherRelay := ""
	voucherPeer := ""
	voucherExpiration := int64(0)
	if reservation.Voucher != nil {
		if payload, err := reservation.Voucher.MarshalRecord(); err == nil {
			voucherPayloadBytes = len(payload)
		}
		voucherRelay = reservation.Voucher.Relay.String()
		voucherPeer = reservation.Voucher.Peer.String()
		voucherExpiration = reservation.Voucher.Expiration.Unix()
	}
	if err := writeJSON(opts.readyFile, map[string]any{
		"implementation":         "go",
		"role":                   "destination",
		"peer_id":                h.ID().String(),
		"listen_addrs":           listenAddrs,
		"relay_addrs":            relayAddrs,
		"relay_peer_id":          relayInfo.ID.String(),
		"native_relay_transport": true,
		"voucher":                reservation.Voucher != nil,
		"voucher_payload_bytes":  voucherPayloadBytes,
		"voucher_relay":          voucherRelay,
		"voucher_peer":           voucherPeer,
		"voucher_expiration":     voucherExpiration,
		"status":                 "ready",
	}); err != nil {
		return err
	}
	for {
		if _, err := os.Stat(opts.stopFile); err == nil {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func openRequiredProtocol(ctx context.Context, h host.Host, peer peer.ID, id protocol.ID) (int, error) {
	stream, err := h.NewStream(ctx, peer, id)
	if err != nil {
		return 0, err
	}
	defer stream.Close()
	if id == protocol.ID("/ipfs/id/1.0.0") {
		payload, err := io.ReadAll(stream)
		if err != nil {
			return 0, err
		}
		if len(payload) == 0 {
			return 0, fmt.Errorf("%s returned empty payload", id)
		}
		return len(payload), nil
	}
	return 0, nil
}

func expectUnsupportedProtocol(ctx context.Context, h host.Host, peer peer.ID, id protocol.ID) (string, error) {
	stream, err := h.NewStream(ctx, peer, id)
	if err == nil {
		stream.Close()
		return "", fmt.Errorf("%s unexpectedly opened", id)
	}
	text := err.Error()
	if strings.Contains(text, "deadline exceeded") || strings.Contains(text, "context canceled") {
		return "", fmt.Errorf("%s failed without protocol rejection proof: %w", id, err)
	}
	return text, nil
}

func dial(opts options) error {
	h, err := newHost()
	if err != nil {
		return err
	}
	defer h.Close()

	addr, err := ma.NewMultiaddr(opts.addr)
	if err != nil {
		return err
	}
	info, err := peer.AddrInfoFromP2pAddr(addr)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := h.Connect(ctx, *info); err != nil {
		return fmt.Errorf("connect failed: %w", err)
	}

	result := map[string]any{
		"implementation": "go",
		"role":           "dialer",
		"scenario":       opts.scenario,
		"local_peer_id":  h.ID().String(),
		"status":         "ok",
	}
	switch opts.scenario {
	case "ping":
		pingService := ping.NewPingService(h)
		ch := pingService.Ping(ctx, info.ID)
		select {
		case pong := <-ch:
			if pong.Error != nil {
				return fmt.Errorf("ping failed: %w", pong.Error)
			}
			result["rtt_ms"] = pong.RTT.Milliseconds()
		case <-ctx.Done():
			return fmt.Errorf("ping timed out: %w", ctx.Err())
		}
	case "identify":
		size, err := openRequiredProtocol(ctx, h, info.ID, protocol.ID("/ipfs/id/1.0.0"))
		if err != nil {
			return err
		}
		result["payload_bytes"] = size
	case "autonatv2":
		if _, err = openRequiredProtocol(ctx, h, info.ID, protocol.ID("/libp2p/autonat/2/dial-request")); err != nil {
			return err
		}
		result["opened"] = true
	case "relay_reserve":
		reservation, err := relayclient.Reserve(ctx, h, *info)
		if err != nil {
			return err
		}
		addrs := make([]string, 0, len(reservation.Addrs))
		for _, addr := range reservation.Addrs {
			addrs = append(addrs, addr.String())
		}
		result["reservation_addrs"] = addrs
		result["limit_duration_ms"] = reservation.LimitDuration.Milliseconds()
		result["limit_data"] = reservation.LimitData
		result["voucher"] = reservation.Voucher != nil
	case "dcutr":
		if _, err = openRequiredProtocol(ctx, h, info.ID, protocol.ID("/libp2p/dcutr")); err != nil {
			return err
		}
		result["opened"] = true
	case "unknown_protocol":
		expected, err := expectUnsupportedProtocol(ctx, h, info.ID, protocol.ID("/fcl/interop/unknown/1"))
		if err != nil {
			return err
		}
		result["expected_error"] = expected
	default:
		return fmt.Errorf("unknown scenario %s", opts.scenario)
	}
	return writeJSON(opts.resultFile, result)
}

func relayedAddr(relayAddr string, target peer.ID) (ma.Multiaddr, error) {
	base, err := ma.NewMultiaddr(relayAddr)
	if err != nil {
		return nil, err
	}
	circuit, err := ma.NewMultiaddr("/p2p-circuit/p2p/" + target.String())
	if err != nil {
		return nil, err
	}
	return base.Encapsulate(circuit), nil
}

func waitDirectConnection(ctx context.Context, h host.Host, target peer.ID) bool {
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	for {
		for _, conn := range h.Network().ConnsToPeer(target) {
			if !strings.Contains(conn.RemoteMultiaddr().String(), "p2p-circuit") {
				return true
			}
		}
		select {
		case <-ctx.Done():
			return false
		case <-ticker.C:
		}
	}
}

func dialRelay(opts options) error {
	h, err := newHost()
	if err != nil {
		return err
	}
	defer h.Close()

	targetPeer, err := peer.Decode(opts.peerID)
	if err != nil {
		return err
	}
	relayPeer, err := peer.Decode(opts.relayPeerID)
	if err != nil {
		return err
	}
	relayAddr, err := ma.NewMultiaddr(opts.relayAddr)
	if err != nil {
		return err
	}
	relayInfo, err := peer.AddrInfoFromP2pAddr(relayAddr)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	if err := h.Connect(ctx, *relayInfo); err != nil {
		return fmt.Errorf("connect relay failed: %w", err)
	}
	destinationAddr, err := relayedAddr(opts.relayAddr, targetPeer)
	if err != nil {
		return err
	}
	targetInfo, err := peer.AddrInfoFromP2pAddr(destinationAddr)
	if err != nil {
		return err
	}
	if err := h.Connect(ctx, *targetInfo); err != nil {
		return fmt.Errorf("connect relayed destination failed: %w", err)
	}

	result := map[string]any{
		"implementation": "go",
		"role":           "relay_dialer",
		"scenario":       opts.scenario,
		"local_peer_id":  h.ID().String(),
		"relay_peer":     relayPeer.String(),
		"target_peer":    targetPeer.String(),
		"relayed_addr":   destinationAddr.String(),
		"status":         "ok",
	}
	stream, err := h.NewStream(network.WithAllowLimitedConn(ctx, "relay-echo"), targetPeer, echoProtocol)
	if err != nil {
		return fmt.Errorf("open relayed echo failed: %w", err)
	}
	payload := []byte("relay-echo")
	if err := writeFrame(stream, payload); err != nil {
		_ = stream.Reset()
		return err
	}
	echoed, err := readFrame(bufio.NewReader(stream))
	if err != nil {
		_ = stream.Reset()
		return err
	}
	_ = stream.Close()
	if string(echoed) != string(payload) {
		return fmt.Errorf("relay echo mismatch: %q", string(echoed))
	}
	result["relay_echo"] = true
	if opts.scenario == "dcutr_relay_topology" {
		if h.holePunch == nil {
			return fmt.Errorf("hole punch service is unavailable")
		}
		if err := h.holePunch.DirectConnect(targetPeer); err != nil {
			return fmt.Errorf("DCUtR direct connect failed: %w", err)
		}
		result["direct_upgrade"] = waitDirectConnection(ctx, h, targetPeer)
		if !result["direct_upgrade"].(bool) {
			return fmt.Errorf("DCUtR did not produce a direct connection")
		}
	}
	return writeJSON(opts.resultFile, result)
}

func main() {
	opts, err := parseArgs()
	if err == nil {
		switch opts.command {
		case "listen":
			err = listen(opts)
		case "destination":
			err = destination(opts)
		case "dial":
			err = dial(opts)
		case "dial-relay":
			err = dialRelay(opts)
		default:
			err = fmt.Errorf("unknown command %s", opts.command)
		}
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
}
