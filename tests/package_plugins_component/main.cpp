import forge.plugins.p2p.node.plugin;
import forge.plugins.crypto.secrets.plugin;
import forge.plugins.log.otlp.plugin;

int main() {
   const auto p2p = forge::plugins::p2p::node::descriptor();
   const auto secrets = forge::plugins::crypto::secrets::descriptor();
   const auto log_otlp = forge::plugins::log::otlp::descriptor();
   return p2p.id.value == "forge.plugins.p2p.node" &&
                secrets.id.value == "forge.plugins.crypto.secrets" &&
                log_otlp.id.value == "forge.plugins.log.otlp"
             ? 0
             : 1;
}
