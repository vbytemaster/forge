import forge.plugins.p2p.node.plugin;

int main() {
   const auto descriptor = forge::plugins::p2p::node::descriptor();
   return descriptor.id.value == "forge.plugins.p2p.node" ? 0 : 1;
}
