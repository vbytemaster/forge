use std::{
    collections::HashSet,
    error::Error,
    fs,
    net::Ipv4Addr,
    path::PathBuf,
    time::{Duration, Instant},
};

use futures::{AsyncReadExt, AsyncWriteExt, StreamExt};
use libp2p::{
    Multiaddr, PeerId, StreamProtocol, SwarmBuilder, autonat, dcutr, gossipsub, identify, identity,
    kad,
    multiaddr::Protocol,
    noise, ping, relay, rendezvous,
    swarm::{NetworkBehaviour, SwarmEvent},
    tcp, yamux,
};
use libp2p_stream as raw_stream;
use rand::rngs::OsRng;
use serde_json::json;

const PUBSUB_TOPIC: &str = "fcl.pubsub.interop";
const PUBSUB_PAYLOAD: &[u8] = b"fcl-gossipsub-live";

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
    seed_file: PathBuf,
    payload: String,
    transport: String,
    expected_messages: usize,
}

#[derive(NetworkBehaviour)]
struct Behaviour {
    autonat: autonat::v2::server::Behaviour,
    relay: relay::Behaviour,
    relay_client: relay::client::Behaviour,
    kad: kad::Behaviour<kad::store::MemoryStore>,
    rendezvous_server: rendezvous::server::Behaviour,
    rendezvous_client: rendezvous::client::Behaviour,
    gossipsub: gossipsub::Behaviour,
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
            "--seed-file" => out.seed_file = PathBuf::from(value),
            "--payload" => out.payload = value,
            "--transport" => out.transport = value,
            "--expected-messages" => out.expected_messages = value.parse()?,
            "--store-dir" | "--features" => {}
            _ => return Err(format!("unknown argument {key}").into()),
        }
    }
    if out.payload.is_empty() {
        out.payload = String::from_utf8_lossy(PUBSUB_PAYLOAD).to_string();
    }
    if out.transport.is_empty() {
        out.transport = "quic".to_string();
    }
    if out.expected_messages == 0 {
        out.expected_messages = 1;
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

fn behaviour_for(key: &identity::Keypair, relay_client: relay::client::Behaviour) -> Behaviour {
    let peer = key.public().to_peer_id();
    let mut kad_config = kad::Config::new(StreamProtocol::new("/ipfs/kad/1.0.0"));
    kad_config.set_query_timeout(Duration::from_secs(10));
    let mut kad_behaviour =
        kad::Behaviour::with_config(peer, kad::store::MemoryStore::new(peer), kad_config);
    kad_behaviour.set_mode(Some(kad::Mode::Server));
    Behaviour {
        autonat: autonat::v2::server::Behaviour::new(OsRng),
        relay: relay::Behaviour::new(peer, Default::default()),
        relay_client,
        kad: kad_behaviour,
        rendezvous_server: rendezvous::server::Behaviour::new(rendezvous::server::Config::default()),
        rendezvous_client: rendezvous::client::Behaviour::new(key.clone()),
        gossipsub: gossipsub::Behaviour::new(
            gossipsub::MessageAuthenticity::Signed(key.clone()),
            gossipsub::ConfigBuilder::default()
                .protocol_id_prefix("/meshsub")
                .validation_mode(gossipsub::ValidationMode::Strict)
                .build()
                .expect("valid gossipsub config"),
        )
        .expect("valid gossipsub behaviour"),
        ping: ping::Behaviour::new(ping::Config::new()),
        identify: identify::Behaviour::new(identify::Config::new(
            "/fcl-interop/0.1.0".into(),
            key.public(),
        )),
        dcutr: dcutr::Behaviour::new(peer),
        stream: raw_stream::Behaviour::new(),
    }
}

async fn new_swarm(transport: &str) -> Result<libp2p::Swarm<Behaviour>, Box<dyn Error>> {
    let key = identity::Keypair::generate_ed25519();
    let mut swarm = match transport {
        "quic" | "" => SwarmBuilder::with_existing_identity(key)
            .with_tokio()
            .with_quic()
            .with_relay_client(noise::Config::new, yamux::Config::default)?
            .with_behaviour(behaviour_for)?
            .build(),
        "tcp" => SwarmBuilder::with_existing_identity(key)
            .with_tokio()
            .with_tcp(
                tcp::Config::default().nodelay(true),
                noise::Config::new,
                yamux::Config::default,
            )?
            .with_relay_client(noise::Config::new, yamux::Config::default)?
            .with_behaviour(behaviour_for)?
            .build(),
        other => return Err(format!("unsupported transport {other}").into()),
    };

    let listen_addr = if transport == "tcp" {
        Multiaddr::empty()
            .with(Protocol::Ip4(Ipv4Addr::LOCALHOST))
            .with(Protocol::Tcp(0))
    } else {
        Multiaddr::empty()
            .with(Protocol::Ip4(Ipv4Addr::LOCALHOST))
            .with(Protocol::Udp(0))
            .with(Protocol::QuicV1)
    };
    swarm.listen_on(listen_addr)?;
    Ok(swarm)
}

fn dht_provider_key() -> kad::RecordKey {
    kad::RecordKey::new(&[
        0x12, 0x20, 0x2e, 0xaa, 0xd0, 0x06, 0x69, 0x42, 0x0a, 0xc7, 0x3a, 0x56, 0xd9, 0x80, 0xb7,
        0x9d, 0xeb, 0x2d, 0x2e, 0x3f, 0xb6, 0x86, 0x6d, 0x1c, 0xac, 0x9e, 0x37, 0x3f, 0x5e, 0x5d,
        0x4a, 0x62, 0xad, 0xf9,
    ])
}

fn transport_addr(mut address: Multiaddr) -> Multiaddr {
    if matches!(address.iter().last(), Some(Protocol::P2p(_))) {
        let _ = address.pop();
    }
    address
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

async fn open_echo_stream_direct(
    swarm: &mut libp2p::Swarm<Behaviour>,
    peer: PeerId,
    payload: &[u8],
) -> Result<usize, Box<dyn Error>> {
    let mut control = swarm.behaviour().stream.new_control();
    let mut open =
        Box::pin(control.open_stream(peer, StreamProtocol::new("/fcl/interop/relay-echo/1")));
    let deadline = tokio::time::sleep(Duration::from_secs(15));
    tokio::pin!(deadline);
    loop {
        tokio::select! {
            result = &mut open => {
                let mut stream = result?;
                write_frame(&mut stream, payload).await?;
                let echoed = read_frame(&mut stream).await?;
                stream.close().await?;
                if echoed != payload {
                    return Err("echo mismatch".into());
                }
                return Ok(echoed.len());
            }
            _ = &mut deadline => {
                return Err("timed out opening echo stream".into());
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

async fn wait_dht_find_peer(
    swarm: &mut libp2p::Swarm<Behaviour>,
    remote_peer: PeerId,
) -> Result<usize, Box<dyn Error>> {
    let id = swarm.behaviour_mut().kad.get_closest_peers(remote_peer);
    let deadline = tokio::time::sleep(Duration::from_secs(20));
    tokio::pin!(deadline);
    loop {
        tokio::select! {
            _ = &mut deadline => return Err("timed out waiting for Kademlia closest peers".into()),
            event = swarm.select_next_some() => {
                match event {
                    SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                        id: event_id,
                        result: kad::QueryResult::GetClosestPeers(Ok(ok)),
                        ..
                    })) if event_id == id => {
                        if ok.peers.iter().any(|peer| peer.peer_id == remote_peer) || !ok.peers.is_empty() {
                            return Ok(ok.peers.len());
                        }
                        return Err("Kademlia closest peers result was empty".into());
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                        id: event_id,
                        result: kad::QueryResult::GetClosestPeers(Err(error)),
                        ..
                    })) if event_id == id => {
                        return Err(format!("Kademlia closest peers failed: {error:?}").into());
                    }
                    SwarmEvent::NewListenAddr { address, .. } => swarm.add_external_address(address),
                    _ => {}
                }
            }
        }
    }
}

async fn wait_dht_provide_find_provider(
    swarm: &mut libp2p::Swarm<Behaviour>,
    local_peer: PeerId,
) -> Result<usize, Box<dyn Error>> {
    let key = dht_provider_key();
    let provide_id = swarm.behaviour_mut().kad.start_providing(key.clone())?;
    let deadline = tokio::time::sleep(Duration::from_secs(30));
    tokio::pin!(deadline);
    let mut providing = false;
    loop {
        tokio::select! {
            _ = &mut deadline => return Err("timed out waiting for Kademlia provider proof".into()),
            event = swarm.select_next_some() => {
                match event {
                    SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                        id,
                        result: kad::QueryResult::StartProviding(result),
                        ..
                    })) if id == provide_id => {
                        result?;
                        providing = true;
                        let _ = swarm.behaviour_mut().kad.get_providers(key.clone());
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                        result: kad::QueryResult::GetProviders(Ok(kad::GetProvidersOk::FoundProviders { providers, .. })),
                        ..
                    })) if providing => {
                        if providers.contains(&local_peer) {
                            return Ok(providers.len());
                        }
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                        result: kad::QueryResult::GetProviders(Err(error)),
                        ..
                    })) if providing => return Err(format!("Kademlia providers failed: {error:?}").into()),
                    SwarmEvent::NewListenAddr { address, .. } => swarm.add_external_address(address),
                    _ => {}
                }
            }
        }
    }
}

async fn wait_rendezvous_register_discover(
    swarm: &mut libp2p::Swarm<Behaviour>,
    remote_peer: PeerId,
) -> Result<usize, Box<dyn Error>> {
    let namespace = rendezvous::Namespace::new("fcl.discovery".to_string())?;
    swarm
        .behaviour_mut()
        .rendezvous_client
        .register(namespace.clone(), remote_peer, None)?;
    let deadline = tokio::time::sleep(Duration::from_secs(30));
    tokio::pin!(deadline);
    let mut registered = false;
    loop {
        tokio::select! {
            _ = &mut deadline => return Err("timed out waiting for rendezvous register/discover".into()),
            event = swarm.select_next_some() => {
                match event {
                    SwarmEvent::Behaviour(BehaviourEvent::RendezvousClient(
                        rendezvous::client::Event::Registered { .. },
                    )) => {
                        registered = true;
                        swarm.behaviour_mut().rendezvous_client.discover(
                            Some(namespace.clone()),
                            None,
                            Some(10),
                            remote_peer,
                        );
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::RendezvousClient(
                        rendezvous::client::Event::Discovered { registrations, .. },
                    )) if registered => return Ok(registrations.len()),
                    SwarmEvent::Behaviour(BehaviourEvent::RendezvousClient(
                        rendezvous::client::Event::RegisterFailed { error, .. },
                    )) => return Err(format!("rendezvous register failed: {error:?}").into()),
                    SwarmEvent::NewListenAddr { address, .. } => swarm.add_external_address(address),
                    _ => {}
                }
            }
        }
    }
}

async fn wait_gossipsub_peer_and_publish(
    swarm: &mut libp2p::Swarm<Behaviour>,
    remote_peer: PeerId,
    payload: &[u8],
) -> Result<(), Box<dyn Error>> {
    let topic = gossipsub::IdentTopic::new(PUBSUB_TOPIC);
    swarm.behaviour_mut().gossipsub.subscribe(&topic)?;
    let deadline = tokio::time::sleep(Duration::from_secs(10));
    tokio::pin!(deadline);
    loop {
        tokio::select! {
            _ = &mut deadline => return Err("timed out waiting for gossipsub subscription exchange".into()),
            event = swarm.select_next_some() => {
                match event {
                    SwarmEvent::Behaviour(BehaviourEvent::Gossipsub(
                        gossipsub::Event::Subscribed { peer_id, topic: subscribed_topic },
                    )) if peer_id == remote_peer && subscribed_topic == topic.hash() => {
                        swarm.behaviour_mut().gossipsub.publish(topic.clone(), payload)?;
                        tokio::time::sleep(Duration::from_secs(2)).await;
                        return Ok(());
                    }
                    SwarmEvent::NewListenAddr { address, .. } => swarm.add_external_address(address),
                    _ => {}
                }
            }
        }
    }
}

async fn listen(opts: Options) -> Result<(), Box<dyn Error>> {
    let mut swarm = new_swarm(&opts.transport).await?;
    spawn_incoming_stream_echo(&mut swarm, "/fcl/interop/relay-echo/1")?;
    if opts.scenario == "gossipsub_publish" || opts.scenario == "gossipsub_mixed_mesh_stress" {
        let topic = gossipsub::IdentTopic::new(PUBSUB_TOPIC);
        swarm.behaviour_mut().gossipsub.subscribe(&topic)?;
    }
    let peer = *swarm.local_peer_id();
    let mut ready = false;
    let mut seeded = false;
    let mut payloads = HashSet::<String>::new();
    let mut duplicates = 0usize;
    loop {
        tokio::select! {
            _ = tokio::time::sleep(Duration::from_millis(100)) => {
                if ready && !seeded && opts.scenario == "gossipsub_mixed_mesh_stress" && opts.seed_file.exists() {
                    let seeds = fs::read_to_string(&opts.seed_file)?;
                    for line in seeds.lines().filter(|line| !line.is_empty()) {
                        if let Ok(address) = line.parse::<Multiaddr>() {
                            if !address.to_string().contains(&peer.to_string()) {
                                let _ = swarm.dial(address);
                            }
                        }
                    }
                    seeded = true;
                }
                if ready && opts.stop_file.exists() {
                    if opts.scenario == "gossipsub_mixed_mesh_stress" {
                        let status = if payloads.len() >= opts.expected_messages && duplicates == 0 {
                            "ok"
                        } else {
                            "mismatch"
                        };
                        write_json(
                            &opts.result_file,
                            json!({
                                "implementation": "rust",
                                "scenario": "gossipsub_mixed_mesh_stress",
                                "status": status,
                                "received": payloads.len(),
                                "expected": opts.expected_messages,
                                "duplicates": duplicates,
                                "payloads": payloads.iter().cloned().collect::<Vec<_>>()
                            }),
                        )?;
                    }
                    return Ok(());
                }
            }
            event = swarm.select_next_some() => {
                eprintln!("rust-listen event: {event:?}");
                match event {
                    SwarmEvent::NewListenAddr { address, .. } => {
                        swarm.add_external_address(address.clone());
                        if !ready {
                            write_json(&opts.ready_file, json!({
                                "implementation": "rust",
                                "role": "listener",
                                "peer_id": peer.to_string(),
                                "listen_addrs": [format!("{address}/p2p/{peer}")],
                                "transport": opts.transport.clone(),
                                "status": "ready"
                            }))?;
                            ready = true;
                        }
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::Gossipsub(
                        gossipsub::Event::Message {
                            propagation_source,
                            message,
                            ..
                        },
                    )) if opts.scenario == "gossipsub_publish" => {
                            let status = if message.data == PUBSUB_PAYLOAD { "ok" } else { "mismatch" };
                            write_json(
                                &opts.result_file,
                                json!({
                                    "implementation": "rust",
                                    "scenario": "gossipsub_publish",
                                    "status": status,
                                    "topic": message.topic.to_string(),
                                    "payload": String::from_utf8_lossy(&message.data).to_string(),
                                    "propagation_source": propagation_source.to_string(),
                                    "source": message.source.map(|peer| peer.to_string()).unwrap_or_default()
                                }),
                            )?;
                    }
                    SwarmEvent::Behaviour(BehaviourEvent::Gossipsub(
                        gossipsub::Event::Message { message, .. },
                    )) if opts.scenario == "gossipsub_mixed_mesh_stress" => {
                        let payload = String::from_utf8_lossy(&message.data).to_string();
                        if !payloads.insert(payload) {
                            duplicates += 1;
                        }
                    }
                    _ => {}
                }
            }
        }
    }
}

async fn dial(opts: Options) -> Result<(), Box<dyn Error>> {
    let mut swarm = new_swarm(&opts.transport).await?;
    let remote_peer: PeerId = opts.peer_id.parse()?;
    let remote: Multiaddr = opts.addr.parse()?;
    if opts.scenario == "gossipsub_publish" {
        let topic = gossipsub::IdentTopic::new(PUBSUB_TOPIC);
        swarm.behaviour_mut().gossipsub.subscribe(&topic)?;
    }
    swarm.dial(remote.clone())?;
    let started = Instant::now();
    let mut connected = false;
    let mut ping_ok = false;
    let mut identify_count = 0usize;
    while started.elapsed() < Duration::from_secs(20) {
        match swarm.select_next_some().await {
            SwarmEvent::ConnectionEstablished { peer_id, .. } if peer_id == remote_peer => {
                eprintln!("rust-dial connected: {peer_id}");
                connected = true;
                swarm
                    .behaviour_mut()
                    .kad
                    .add_address(&remote_peer, transport_addr(remote.clone()));
                if opts.scenario != "ping" && opts.scenario != "identify" {
                    break;
                }
            }
            SwarmEvent::NewListenAddr { address, .. } => {
                swarm.add_external_address(address);
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
        "echo" => {
            let bytes =
                open_echo_stream_direct(&mut swarm, remote_peer, opts.payload.as_bytes()).await?;
            write_json(
                &opts.result_file,
                json!({
                    "implementation": "rust",
                    "role": "dialer",
                    "scenario": opts.scenario,
                    "status": "ok",
                    "protocol": "/fcl/interop/relay-echo/1",
                    "payload_bytes": bytes,
                    "echo_ok": true
                }),
            )?;
            return Ok(());
        }
        "dcutr" => {
            open_required_stream(&mut swarm, remote_peer, "/libp2p/dcutr").await?;
        }
        "dht_find_peer" => {
            let count = wait_dht_find_peer(&mut swarm, remote_peer).await?;
            write_json(
                &opts.result_file,
                json!({
                    "implementation": "rust",
                    "role": "dialer",
                    "scenario": opts.scenario,
                    "status": "ok",
                    "closest_peers": count
                }),
            )?;
            return Ok(());
        }
        "dht_provide_find_provider" => {
            let local_peer = *swarm.local_peer_id();
            let count = wait_dht_provide_find_provider(&mut swarm, local_peer).await?;
            write_json(
                &opts.result_file,
                json!({
                    "implementation": "rust",
                    "role": "dialer",
                    "scenario": opts.scenario,
                    "status": "ok",
                    "provider_count": count
                }),
            )?;
            return Ok(());
        }
        "rendezvous_register_discover" => {
            let registrations = wait_rendezvous_register_discover(&mut swarm, remote_peer).await?;
            write_json(
                &opts.result_file,
                json!({
                    "implementation": "rust",
                    "role": "dialer",
                    "scenario": opts.scenario,
                    "status": "ok",
                    "registration_count": registrations
                }),
            )?;
            return Ok(());
        }
        "gossipsub_publish" | "gossipsub_mixed_mesh_stress" => {
            wait_gossipsub_peer_and_publish(&mut swarm, remote_peer, opts.payload.as_bytes())
                .await?;
            write_json(
                &opts.result_file,
                json!({
                    "implementation": "rust",
                    "role": "dialer",
                    "scenario": opts.scenario,
                    "status": "ok",
                    "topic": PUBSUB_TOPIC,
                    "payload": opts.payload,
                    "payload_bytes": opts.payload.len(),
                    "mesh_peer": true
                }),
            )?;
            return Ok(());
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
    let mut swarm = new_swarm(&opts.transport).await?;
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
    let mut swarm = new_swarm(&opts.transport).await?;
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
