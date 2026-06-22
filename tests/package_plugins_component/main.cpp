import fcl.plugins.p2p.node.plugin;
import fcl.plugins.secret.provider.plugin;

int main() {
   const auto p2p = fcl::plugins::p2p::node::descriptor();
   const auto secrets = fcl::plugins::secret::provider::descriptor();
   return p2p.id.value == "fcl.plugins.p2p.node" &&
                secrets.id.value == "fcl.plugins.secret.provider"
             ? 0
             : 1;
}
