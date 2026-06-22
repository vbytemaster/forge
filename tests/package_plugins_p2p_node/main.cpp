import fcl.plugins.p2p.node.plugin;

int main() {
   const auto descriptor = fcl::plugins::p2p::node::descriptor();
   return descriptor.id.value == "fcl.plugins.p2p.node" ? 0 : 1;
}
