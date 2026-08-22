import forge.plugins.p2p.node.plugin;
import forge.plugins.p2p.node.types;

int main() {
   const auto descriptor = forge::plugins::p2p::node::descriptor();
   const auto config = forge::plugins::p2p::node::config{};
   return descriptor.id.value == "forge.plugins.p2p.node" &&
                  config.topology_mode == forge::plugins::p2p::node::topology_mode::managed &&
                  config.topology_target == 160
              ? 0
              : 1;
}
