import forge.net.p2p.dht;
import forge.net.p2p.dht.record_store;
import forge.net.p2p.identity;
import forge.net.p2p.ipns;
import forge.net.p2p.provider_registration;
import forge.net.p2p.topology;

int main() {
   const auto id = forge::net::p2p::peer_id{};
   auto store = forge::net::p2p::dht::record_store{
       forge::net::p2p::amino_v1(), {.persistence = forge::net::p2p::dht::record_store::make_memory_persistence()}};
   auto registration = forge::net::p2p::provider_registration{};
   const auto topology = forge::net::p2p::topology::policy{};
   return id.value.empty() && !registration.active() && forge::net::p2p::ipns::routing_prefix.size() == 6 &&
                  !store.persistence_state().closed &&
                  topology.operating_mode == forge::net::p2p::topology::mode::managed &&
                  topology.peers.low == 128 && topology.peers.target == 160 && topology.peers.high == 192
              ? 0
              : 1;
}
