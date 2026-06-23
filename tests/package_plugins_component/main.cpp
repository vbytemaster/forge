import fcl.plugins.p2p.node.plugin;
import fcl.plugins.crypto.secrets.plugin;

int main() {
   const auto p2p = fcl::plugins::p2p::node::descriptor();
   const auto secrets = fcl::plugins::crypto::secrets::descriptor();
   return p2p.id.value == "fcl.plugins.p2p.node" &&
                secrets.id.value == "fcl.plugins.crypto.secrets"
             ? 0
             : 1;
}
