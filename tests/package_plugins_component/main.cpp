import fcl.plugins;

int main() {
   const auto descriptor = fcl::plugins::p2p_node::descriptor();
   return descriptor.id.value == "fcl.p2p_node" ? 0 : 1;
}
