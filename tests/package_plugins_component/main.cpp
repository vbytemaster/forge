import forge.plugins.p2p.node.plugin;
import forge.plugins.crypto.secrets.plugin;
import forge.plugins.log.otlp.plugin;
#if FORGE_PACKAGE_HAS_DB_ROCKSDB
import forge.plugins.db.rocksdb.plugin;
#endif

int main() {
   const auto p2p = forge::plugins::p2p::node::descriptor();
   const auto secrets = forge::plugins::crypto::secrets::descriptor();
   const auto log_otlp = forge::plugins::log::otlp::descriptor();
#if FORGE_PACKAGE_HAS_DB_ROCKSDB
   const auto rocksdb = forge::plugins::db::rocksdb::descriptor();
#endif
   return p2p.id.value == "forge.plugins.p2p.node" &&
                secrets.id.value == "forge.plugins.crypto.secrets" &&
                log_otlp.id.value == "forge.plugins.log.otlp"
#if FORGE_PACKAGE_HAS_DB_ROCKSDB
                && rocksdb.id.value == "forge.plugins.db.rocksdb"
#endif
             ? 0
             : 1;
}
