import fcl.otlp.options;

int main() {
   const auto options = fcl::otlp::log_exporter_options{};
   return options.logs_path == "/v1/logs" ? 0 : 1;
}
