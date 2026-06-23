import forge.plugins.p2p.node.plugin;
import forge.plugins.crypto.secrets.plugin;

int main() {
   const auto p2p = forge::plugins::p2p::node::descriptor();
   const auto secrets = forge::plugins::crypto::secrets::descriptor();
   return p2p.id.value == "forge.plugins.p2p.node" &&
                secrets.id.value == "forge.plugins.crypto.secrets"
             ? 0
             : 1;
}
