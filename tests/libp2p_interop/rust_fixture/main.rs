use std::{
    error::Error,
    fs,
    net::Ipv4Addr,
    path::PathBuf,
    time::{Duration, Instant},
};

use futures::{AsyncReadExt, AsyncWriteExt, StreamExt};
use libp2p::{
    autonat, dcutr, identify, identity,
    multiaddr::Protocol,
    noise, ping, relay,
    swarm::{NetworkBehaviour, SwarmEvent},
    yamux, Multiaddr, PeerId, StreamProtocol, SwarmBuilder,
};
use libp2p_stream as raw_stream;
use rand::rngs::OsRng;
use serde_json::json;

#[derive(Debug, Default)]
struct Options {
    command: String,
    scenario: String,
    peer_id: String,
    addr: String,
    relay_addr: String,
    relay_peer_id: String,
    ready_file: PathBuf,
    stop_file: PathBuf,
    result_file: PathBuf,
}

#[derive(NetworkBehaviour)]
struct Behaviour {
    autonat: autonat::v2::server::Behaviour,
    relay: relay::Behaviour,
    relay_client: relay::client::Behaviour,
    ping: ping::Behaviour,
    identify: identify::Behaviour,
    dcutr: dcutr::Behaviour,
    stream: raw_stream::Behaviour,
}

fn parse_args() -> Result<Options, Box<dyn Error>> {
    let mut args = std::env::args().skip(1);
    let mut out = Options::default();
    out.command = args.next().ok_or("missing command")?;
    while let Some(key) = args.next() {
        let value = args
            .next()
            .ok_or_else(|| format!("missing value for {key}"))?;
        match key.as_str() {
            "--scenario" => out.scenario = value,
            "--peer-id" => out.peer_id = value,
            "--addr" => out.addr = value,
            "--relay-addr" => out.relay_addr = value,
            "--relay-peer-id" => out.relay_peer_id = value,
            "--ready-file" => out.ready_file = PathBuf::from(value),
            "--stop-file" => out.stop_file = PathBuf::from(value),
            "--result-file" => out.result_file = PathBuf::from(value),
            "--store-dir" | "--features" => {}
            _ => return Err(format!("unknown argument {key}").into()),
        }
    }
    Ok(out)
}

fn write_json(path: &PathBuf, value: serde_json::Value) -> Result<(), Box<dyn Error>> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, serde_json::to_vec(&value)?)?;
    Ok(())
}

async fn new_swarm() -> Result<libp2p::Swarm<Behaviour>, Box<dyn Error>> {
    let key = identity::Keypair::generate_ed25519();
    let mut swarm = SwarmBuilder::with_existing_identity(key)
        .with_tokio()
        .with_quic()
        .with_relay_client(noise::Config::new, yamux::Config::default)?
        .with_behaviour(|key, relay_client| {
            let peer = key.public().to_peer_id();
            Behaviour {
                autonat: autonat::v2::server::Behaviour::new(OsRng),
                relay: relay::Behaviour::new(peer, Default::default()),
                relay_client,
                ping: ping::Behaviour::new(ping::Config::new()),
                identify: identify::Behaviour::new(identify::Config::new(
                    "/fcl-interop/0.1.0".into(),
                    key.public(),
                )),
                dcutr: dcutr::Behaviour::new(peer),
                stream: raw_stream::Behaviour::new(),
            }
        })?
        .build();

    swarm.listen_on(
        Multiaddr::empty()
            .with(Protocol::Ip4(Ipv4Addr::LOCALHOST))
            .with(Protocol::Udp(0))
            .with(Protocol::QuicV1),
    )?;
    Ok(swarm)
}

async fn read_frame<S>(stream: &mut S) -> Result<Vec<u8>, Box<dyn Error>>
where
    S: futures::AsyncRead + Unpin,
{
    let mut shift = 0u32;
    let mut size = 0usize;
    loop {
        let mut single = [0u8; 1];
        stream.read_exact(&mut single).await?;
        let byte = single[0];
        size |= usize::from(byte & 0x7f) << shift;
        if byte & 0x80 == 0 {
            break;
        }
        shift += 7;
        if shift > 28 {
            return Err("frame varint is too large".into());
        }
    }
    if size == 0 || size > 16 * 1024 {
        return Err(format!("invalid frame size {size}").into());
    }
    let mut payload = vec![0; size];
    stream.read_exact(&mut payload).await?;
    Ok(payload)
}

async fn write_frame<S>(stream: &mut S, payload: &[u8]) -> Result<(), Box<dyn Error>>
where
    S: futures::AsyncWrite + Unpin,
{
    let mut value = payload.len();
    let mut prefix = Vec::new();
    loop {
        let mut byte = (value & 0x7f) as u8;
        value >>= 7;
        if value != 0 {
            byte |= 0x80;
        }
        prefix.push(byte);
        if value == 0 {
            break;
        }
    }
    stream.write_all(&prefix).await?;
    stream.write_all(payload).await?;
    stream.flush().await?;
    Ok(())
}

fn spawn_incoming_stream_echo(
    swarm: &mut libp2p::Swarm<Behaviour>,
    protocol: &'static str,
) -> Result<(), Box<dyn Error>> {
    let mut control = swarm.behaviour().stream.new_control();
    let mut incoming = control.accept(StreamProtocol::new(protocol))?;
    tokio::spawn(async move {
        while let Some((_, mut stream)) = incoming.next().await {
            let payload = match read_frame(&mut stream).await.ok() {
                Some(value) => value,
                None => {
                    let _ = stream.close().await;
                    continue;
                }
            };
            let _ = write_frame(&mut stream, &payload).await;
            let _ = stream.close().await;
        }
    });
    Ok(())
}

async fn open_required_stream(
    swarm: &mut libp2p::Swarm<Behaviour>,
    peer: PeerId,
    protocol: &'static str,
) -> Result<(), Box<dyn Error>> {
    let mut control = swarm.behaviour().stream.new_control();
    let mut open = Box::pin(control.open_stream(peer, StreamProtocol::new(protocol)));
    let deadline = tokio::time::sleep(Duration::from_secs(15));
    tokio::pin!(deadline);
    loop {
        tokio::select! {
            result = &mut open => {
                let mut stream = result?;
                stream.close().await?;
                return Ok(());
            }
            _ = &mut deadline => {
                return Err(format!("timed out opening {protocol}").into());
            }
            event = swarm.select_next_some() => {
                if let SwarmEvent::NewListenAddr { address, .. } = event {
                    swarm.add_external_address(address);
                }
            }
        }
    }
}

async fn expect_unknown_stream_rejection(
    swarm: &mut libp2p::Swarm<Behaviour>,
    peer: PeerId,
    protocol: &'static str,
) -> Result<String, Box<dyn Error>> {
    let mut control = swarm.behaviour().stream.new_control();
    let mut open = Box::pin(control.open_stream(peer, StreamProtocol::new(protocol)));
    let deadline = tokio::time::sleep(Duration::from_secs(15));
    tokio::pin!(deadline);
    loop {
        tokio::select! {
            result = &mut open => {
                match result {
                    Ok(mut stream) => {
                        let _ = stream.close().await;
                        return Err(format!("{protocol} unexpectedly opened").into());
                    }
                    Err(error) => return Ok(error.to_string()),
                }
            }
            _ = &mut deadline => {
                return Err(format!("timed out waiting for {protocol} rejection").into());
            }
            event = swarm.select_next_some() => {
                if let SwarmEvent::NewListenAddr { address, .. } = event {
                    swarm.add_external_address(address);
                }
            }
        }
    }
}

async fn listen(opts: Options) -> Result<(), Box<dyn Error>> {
    let mut swarm = new_swarm().await?;
    spawn_incoming_stream_echo(&mut swarm, "/fcl/interop/relay-echo/1")?;
    let peer = *swarm.local_peer_id();
    let mut ready = false;
    loop {
        tokio::select! {
            _ = tokio::time::sleep(Duration::from_millis(100)) => {
                if ready && opts.stop_file.exists() {
                    return Ok(());
                }
            }
            event = swarm.select_next_some() => {
                eprintln!("rust-listen event: {event:?}");
                if let SwarmEvent::NewListenAddr { address, .. } = event {
                    swarm.add_external_address(address.clone());
                    if !ready {
                        write_json(&opts.ready_file, json!({
                            "implementation": "rust",
                            "role": "listener",
                            "peer_id": peer.to_string(),
                            "listen_addrs": [format!("{address}/p2p/{peer}")],
                            "status": "ready"
                        }))?;
                        ready = true;
                    }
                }
            }
        }
    }
}

async fn dial(opts: Options) -> Result<(), Box<dyn Error>> {
    let mut swarm = new_swarm().await?;
    let remote_peer: PeerId = opts.peer_id.parse()?;
    let remote: Multiaddr = opts.addr.parse()?;
    swarm.dial(remote)?;
    let started = Instant::now();
    let mut connected = false;
    let mut ping_ok = false;
    let mut identify_count = 0usize;
    while started.elapsed() < Duration::from_secs(20) {
        match swarm.select_next_some().await {
            SwarmEvent::ConnectionEstablished { peer_id, .. } if peer_id == remote_peer => {
                eprintln!("rust-dial connected: {peer_id}");
                connected = true;
                if opts.scenario != "ping" && opts.scenario != "identify" {
                    break;
                }
            }
            SwarmEvent::Behaviour(BehaviourEvent::Ping(ping::Event { peer, result, .. }))
                if peer == remote_peer =>
            {
                eprintln!("rust-dial ping: {result:?}");
                ping_ok = result.is_ok();
                if opts.scenario == "ping" {
                    break;
                }
            }
            SwarmEvent::Behaviour(BehaviourEvent::Identify(identify::Event::Received {
                peer_id,
                info,
                ..
            })) if peer_id == remote_peer => {
                eprintln!("rust-dial identify: protocols={}", info.protocols.len());
                identify_count = info.protocols.len();
                if opts.scenario == "identify" {
                    break;
                }
            }
            other => {
                eprintln!("rust-dial event: {other:?}");
            }
        }
    }
    if !connected {
        return Err("connection was not established".into());
    }
    match opts.scenario.as_str() {
        "ping" if !ping_ok => return Err("ping did not complete".into()),
        "identify" if identify_count == 0 => return Err("identify did not return protocols".into()),
        "autonatv2" => {
            open_required_stream(&mut swarm, remote_peer, "/libp2p/autonat/2/dial-request").await?;
        }
        "relay_reserve" => {
            open_required_stream(&mut swarm, remote_peer, "/libp2p/circuit/relay/0.2.0/hop")
                .await?;
        }
        "dcutr" => {
            open_required_stream(&mut swarm, remote_peer, "/libp2p/dcutr").await?;
        }
        "unknown_protocol" => {
            let error =
                expect_unknown_stream_rejection(&mut swarm, remote_peer, "/fcl/interop/unknown/1")
                    .await?;
            write_json(
                &opts.result_file,
                json!({
                    "implementation": "rust",
                    "role": "dialer",
                    "scenario": opts.scenario,
                    "status": "ok",
                    "expected_error": error
                }),
            )?;
            return Ok(());
        }
        _ => {}
    }
    write_json(
        &opts.result_file,
        json!({
            "implementation": "rust",
            "role": "dialer",
            "scenario": opts.scenario,
            "status": "ok",
            "ping_ok": ping_ok,
            "protocol_count": identify_count
        }),
    )
}

async fn destination(opts: Options) -> Result<(), Box<dyn Error>> {
    let mut swarm = new_swarm().await?;
    spawn_incoming_stream_echo(&mut swarm, "/fcl/interop/relay-echo/1")?;
    let peer = *swarm.local_peer_id();
    let relay_addr: Multiaddr = opts.relay_addr.parse()?;
    swarm.listen_on(relay_addr.clone().with(Protocol::P2pCircuit))?;
    let started = Instant::now();
    let mut relay_addrs = Vec::new();
    let mut reservation = false;
    while started.elapsed() < Duration::from_secs(30) {
        let event = swarm.select_next_some().await;
        eprintln!("rust-destination event: {:?}", event);
        match event {
            SwarmEvent::NewListenAddr { address, .. } => {
                swarm.add_external_address(address.clone());
                if address.to_string().contains("p2p-circuit") {
                    relay_addrs.push(address.to_string());
                    reservation = true;
                }
            }
            SwarmEvent::ExternalAddrConfirmed { address } => {
                if address.to_string().contains("p2p-circuit") {
                    relay_addrs.push(address.to_string());
                    reservation = true;
                }
            }
            SwarmEvent::Behaviour(BehaviourEvent::RelayClient(
                relay::client::Event::ReservationReqAccepted { .. },
            )) => {
                reservation = true;
                if relay_addrs.is_empty() {
                    relay_addrs.push(
                        relay_addr
                            .clone()
                            .with(Protocol::P2pCircuit)
                            .with(Protocol::P2p(peer))
                            .to_string(),
                    );
                }
                if !relay_addrs.is_empty() {
                    break;
                }
            }
            SwarmEvent::Behaviour(BehaviourEvent::Identify(identify::Event::Received {
                info,
                ..
            })) => {
                swarm.add_external_address(info.observed_addr);
            }
            _ => {}
        }
    }
    if !reservation || relay_addrs.is_empty() {
        return Err("relay reservation was not established".into());
    }
    write_json(
        &opts.ready_file,
        json!({
            "implementation": "rust",
            "role": "destination",
            "peer_id": peer.to_string(),
            "relay_addrs": relay_addrs,
            "relay_peer_id": opts.relay_peer_id,
            "native_relay_transport": true,
            "status": "ready"
        }),
    )?;
    loop {
        tokio::select! {
            _ = tokio::time::sleep(Duration::from_millis(100)) => {
                if opts.stop_file.exists() {
                    return Ok(());
                }
            }
            event = swarm.select_next_some() => {
                if let SwarmEvent::NewListenAddr { address, .. } = event {
                    swarm.add_external_address(address);
                }
            }
        }
    }
}

async fn open_echo_stream(
    swarm: &mut libp2p::Swarm<Behaviour>,
    peer: PeerId,
    expect_direct_upgrade: bool,
) -> Result<bool, Box<dyn Error>> {
    let mut control = swarm.behaviour().stream.new_control();
    let mut open =
        Box::pin(control.open_stream(peer, StreamProtocol::new("/fcl/interop/relay-echo/1")));
    let deadline = tokio::time::sleep(Duration::from_secs(30));
    tokio::pin!(deadline);
    let mut direct_upgrade = false;
    loop {
        tokio::select! {
            result = &mut open => {
                let mut stream = result?;
                write_frame(&mut stream, b"relay-echo").await?;
                let echoed = read_frame(&mut stream).await?;
                stream.close().await?;
                if echoed != b"relay-echo" {
                    return Err("relay echo mismatch".into());
                }
                if expect_direct_upgrade && !direct_upgrade {
                    let settle = tokio::time::sleep(Duration::from_secs(5));
                    tokio::pin!(settle);
                    loop {
                        tokio::select! {
                            _ = &mut settle => return Ok(direct_upgrade),
	                            event = swarm.select_next_some() => {
	                                if let SwarmEvent::ConnectionEstablished { peer_id, endpoint, .. } = event {
	                                    if peer_id == peer && !format!("{endpoint:?}").contains("P2pCircuit") {
	                                        return Ok(true);
	                                    }
	                                }
	                            }
                        }
                    }
                }
                return Ok(direct_upgrade);
            }
            _ = &mut deadline => {
                return Err("timed out opening relay echo stream".into());
            }
            event = swarm.select_next_some() => {
                match event {
                    SwarmEvent::ConnectionEstablished { peer_id, endpoint, .. } if peer_id == peer => {
                        if !format!("{endpoint:?}").contains("P2pCircuit") {
                            direct_upgrade = true;
                        }
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::Identify(identify::Event::Received { info, .. })) => {
                        swarm.add_external_address(info.observed_addr);
                    }
                    _ => {}
                }
            }
        }
    }
}

async fn dial_and_wait(
    swarm: &mut libp2p::Swarm<Behaviour>,
    peer: PeerId,
    address: Multiaddr,
) -> Result<(), Box<dyn Error>> {
    swarm.dial(address)?;
    let deadline = tokio::time::sleep(Duration::from_secs(20));
    tokio::pin!(deadline);
    loop {
        tokio::select! {
            _ = &mut deadline => return Err("timed out connecting relay".into()),
            event = swarm.select_next_some() => {
                match event {
                    SwarmEvent::ConnectionEstablished { peer_id, .. } if peer_id == peer => return Ok(()),
                    SwarmEvent::Behaviour(BehaviourEvent::Identify(identify::Event::Received { info, .. })) => {
                        swarm.add_external_address(info.observed_addr);
                    }
                    _ => {}
                }
            }
        }
    }
}

async fn dial_relay(opts: Options) -> Result<(), Box<dyn Error>> {
    let mut swarm = new_swarm().await?;
    let target_peer: PeerId = opts.peer_id.parse()?;
    let relay_peer: PeerId = opts.relay_peer_id.parse()?;
    let relay_addr: Multiaddr = opts.relay_addr.parse()?;
    dial_and_wait(&mut swarm, relay_peer, relay_addr.clone()).await?;
    let target_addr = relay_addr
        .with(Protocol::P2pCircuit)
        .with(Protocol::P2p(target_peer));
    swarm.dial(target_addr.clone())?;
    let direct_upgrade = open_echo_stream(
        &mut swarm,
        target_peer,
        opts.scenario == "dcutr_relay_topology",
    )
    .await?;
    if opts.scenario == "dcutr_relay_topology" && !direct_upgrade {
        return Err("DCUtR did not produce a direct connection".into());
    }
    write_json(
        &opts.result_file,
        json!({
            "implementation": "rust",
            "role": "relay_dialer",
            "scenario": opts.scenario,
            "status": "ok",
            "relay_peer": relay_peer.to_string(),
            "target_peer": target_peer.to_string(),
            "relayed_addr": target_addr.to_string(),
            "relay_echo": true,
            "direct_upgrade": direct_upgrade
        }),
    )
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let opts = parse_args()?;
    match opts.command.as_str() {
        "listen" => listen(opts).await,
        "destination" => destination(opts).await,
        "dial" => dial(opts).await,
        "dial-relay" => dial_relay(opts).await,
        _ => Err(format!("unknown command {}", opts.command).into()),
    }
}
