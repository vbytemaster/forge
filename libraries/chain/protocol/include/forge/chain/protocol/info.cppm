module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

export module forge.chain.protocol.info;

export import forge.chain.protocol.audit;
export import forge.chain.protocol.time;

export namespace forge::chain::protocol {

struct info_response : audited_response {
   chain_id chain;
   std::string server_version;
   std::string server_version_string;
   std::string server_full_version_string;
   block_id head;
   std::uint32_t head_num = 0;
   forge::chain::protocol::time_point head_time{};
   forge::chain::protocol::account_name head_producer;
   block_id finalized;
   std::uint32_t finalized_num = 0;
   forge::chain::protocol::time_point finalized_time{};
   std::optional<block_id> best_candidate;
   std::optional<std::uint32_t> best_candidate_num;
   std::uint32_t earliest_available_block_num = 0;
   std::uint64_t virtual_block_cpu_limit = 0;
   std::uint64_t virtual_block_net_limit = 0;
   std::uint64_t block_cpu_limit = 0;
   std::uint64_t block_net_limit = 0;
   std::uint64_t total_cpu_weight = 0;
   std::uint64_t total_net_weight = 0;
   capabilities available;
   service_limits limits;

   bool operator==(const info_response&) const = default;
};

BOOST_DESCRIBE_STRUCT(info_response, (audited_response),
                      (chain, server_version, server_version_string, server_full_version_string, head, head_num,
                       head_time, head_producer, finalized, finalized_num, finalized_time, best_candidate,
                       best_candidate_num, earliest_available_block_num, virtual_block_cpu_limit,
                       virtual_block_net_limit, block_cpu_limit, block_net_limit, total_cpu_weight, total_net_weight,
                       available, limits))

} // namespace forge::chain::protocol
