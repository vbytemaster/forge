module;

module fcl.p2p.scoring;

namespace fcl::p2p {

double score_path(const path::observation& observation) noexcept {
   auto score = 1'000.0;
   score -= static_cast<double>(observation.latency.count());
   score -= static_cast<double>(observation.failures) * 125.0;
   score += static_cast<double>(observation.successes) * 15.0;
   score -= static_cast<double>(observation.in_flight) * 10.0;
   if (observation.kind == path::kind::relay) {
      score -= 100.0;
   }
   if (observation.last_success) {
      score += 50.0;
   }
   return score;
}

} // namespace fcl::p2p
