#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "authenticated_benchmark_options.hpp"

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.authenticated.codec;
import forge.db.authenticated.store;
import forge.db.authenticated.transaction;
import forge.db.authenticated.types;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.mdbx.driver;
import forge.exceptions;

namespace {

using clock_type = std::chrono::steady_clock;
using forge::db::authenticated::bytes;
using forge::test::db_authenticated::benchmark::baseline_name;
using forge::test::db_authenticated::benchmark::baseline_profile;
using forge::test::db_authenticated::benchmark::chunk_keys_source;
using forge::test::db_authenticated::benchmark::committed_version_count;
using forge::test::db_authenticated::benchmark::expected_retained_version;
using forge::test::db_authenticated::benchmark::options;
using forge::test::db_authenticated::benchmark::parse_options;
using forge::test::db_authenticated::benchmark::usage;
using forge::test::db_authenticated::benchmark::usage_error;
using forge::test::db_authenticated::benchmark::validate_retained_root;

constexpr auto point_proof_count = std::size_t{1'000};
constexpr auto range_proof_count = std::size_t{100};
constexpr auto range_proof_limit = std::uint32_t{256};
constexpr auto mebibyte = std::uint64_t{1} << 20U;
constexpr auto gibibyte = std::uint64_t{1} << 30U;

struct provisional_gate_thresholds {
   double initial_batch_min_keys_per_second = 0.0;
   double point_proofs_min_per_second = 0.0;
   double range_proofs_min_per_second = 0.0;
};

struct proof_measurement {
   std::chrono::nanoseconds elapsed{};
   std::uint64_t wire_bytes = 0;
   std::uint64_t nodes = 0;
};

struct benchmark_result {
   forge::db::authenticated::root root;
   std::size_t committed_versions = 0;
   std::size_t verified_retained_versions = 0;
   std::size_t retention_content_checks = 0;
   std::chrono::nanoseconds initial_batch{};
   std::chrono::nanoseconds initial_load_wall{};
   std::chrono::nanoseconds mutation_construction{};
   std::chrono::nanoseconds retention_verification{};
   proof_measurement point_proofs;
   proof_measurement range_proofs;
};

struct database_footprint {
   std::uintmax_t logical_bytes = 0;
   std::size_t files = 0;
};

class temporary_path_guard {
 public:
   temporary_path_guard(std::filesystem::path path, bool enabled) : path_{std::move(path)}, enabled_{enabled} {}

   ~temporary_path_guard() {
      if (enabled_) {
         auto error = std::error_code{};
         std::filesystem::remove_all(path_, error);
      }
   }

   temporary_path_guard(const temporary_path_guard&) = delete;
   temporary_path_guard& operator=(const temporary_path_guard&) = delete;

 private:
   std::filesystem::path path_;
   bool enabled_ = false;
};

std::optional<provisional_gate_thresholds> provisional_thresholds(baseline_profile baseline) {
   switch (baseline) {
   case baseline_profile::custom:
      return std::nullopt;
   case baseline_profile::one_million:
   case baseline_profile::ten_million:
      // These intentionally broad floors detect stalled or grossly regressed runs. They are not product SLOs.
      return provisional_gate_thresholds{
          .initial_batch_min_keys_per_second = 250.0,
          .point_proofs_min_per_second = 2.0,
          .range_proofs_min_per_second = 0.2,
      };
   }
   throw std::logic_error{"unknown authenticated benchmark baseline"};
}

bytes make_key(std::uint64_t index) {
   auto result = bytes(sizeof(index));
   for (auto offset = std::size_t{}; offset < result.size(); ++offset) {
      const auto shift = static_cast<unsigned>((result.size() - offset - 1U) * 8U);
      result[offset] = static_cast<std::byte>((index >> shift) & 0xffU);
   }
   return result;
}

bytes make_value(std::uint64_t index, std::size_t size) {
   auto result = bytes(size);
   auto state = index ^ 0x9e3779b97f4a7c15ULL;
   for (auto& value : result) {
      state ^= state >> 12U;
      state ^= state << 25U;
      state ^= state >> 27U;
      value = static_cast<std::byte>((state * 0x2545f4914f6cdd1dULL) >> 56U);
   }
   return result;
}

std::vector<forge::db::authenticated::mutation> make_mutations(const options& settings, std::size_t first,
                                                               std::size_t count) {
   auto result = std::vector<forge::db::authenticated::mutation>{};
   result.reserve(count);
   for (auto offset = std::size_t{}; offset < count; ++offset) {
      const auto index = first + offset;
      result.push_back({
          .key = make_key(index),
          .value = make_value(index, settings.value_bytes),
      });
   }
   return result;
}

std::size_t sample_position(std::size_t sample, std::size_t samples, std::size_t population) {
   const auto quotient = population / samples;
   const auto remainder = population % samples;
   return quotient * sample + (remainder * sample) / samples;
}

std::size_t inclusive_sample_position(std::size_t sample, std::size_t samples, std::size_t maximum) {
   if (samples <= 1U) {
      return 0U;
   }
   const auto denominator = samples - 1U;
   const auto quotient = maximum / denominator;
   const auto remainder = maximum % denominator;
   return quotient * sample + (remainder * sample) / denominator;
}

std::uint64_t mdbx_growth_step(std::size_t keys) {
   return keys >= 1'000'000U ? gibibyte : 64U * mebibyte;
}

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw forge::exceptions::context_error{std::string{message}};
   }
}

database_footprint measure_database_footprint(const std::filesystem::path& path) {
   auto result = database_footprint{};
   for (const auto& entry : std::filesystem::recursive_directory_iterator{path}) {
      if (entry.is_regular_file()) {
         result.logical_bytes += entry.file_size();
         ++result.files;
      }
   }
   return result;
}

boost::asio::awaitable<forge::db::authenticated::root> commit_database_chunk(
    const forge::db::authenticated::store& authenticated, const std::shared_ptr<forge::db::mdbx::driver>& driver,
    forge::db::authenticated::version_id_t version, std::span<const forge::db::authenticated::mutation> mutations,
    std::chrono::nanoseconds& database_elapsed) {
   const auto started = clock_type::now();
   auto db_transaction = co_await driver->begin_transaction();
   auto authenticated_transaction = co_await authenticated.join(db_transaction, version);
   const auto staged = co_await authenticated_transaction.stage(mutations);
   co_await db_transaction.commit();
   database_elapsed += clock_type::now() - started;
   co_return staged.commitment;
}

boost::asio::awaitable<void> verify_retained_versions(const options& settings,
                                                      const forge::db::authenticated::store& authenticated,
                                                      benchmark_result& result) {
   const auto started = clock_type::now();
   const auto expected_versions = committed_version_count(settings.keys, settings.load_chunk_keys);
   for (auto version = std::size_t{}; version < expected_versions; ++version) {
      const auto expected = expected_retained_version(version, settings.keys, settings.load_chunk_keys);
      const auto actual = co_await authenticated.find_root(expected.version);
      validate_retained_root(actual, expected);

      const auto last_key_index = expected.state_size - 1U;
      const auto last_value = co_await authenticated.get(expected.version, make_key(last_key_index));
      require(last_value && *last_value == make_value(last_key_index, settings.value_bytes),
              "authenticated benchmark historical root does not contain its last committed key");
      ++result.retention_content_checks;

      if (expected.state_size < settings.keys) {
         const auto next_value = co_await authenticated.get(expected.version, make_key(expected.state_size));
         require(!next_value, "authenticated benchmark historical root contains a future key");
         ++result.retention_content_checks;
      }

      const auto first_change_key = make_key(expected.state_size - expected.change_count);
      const auto changes = co_await authenticated.scan_range(expected.version,
                                                             {
                                                                 .lower = first_change_key,
                                                                 .limit = 1U,
                                                                 .include_values = false,
                                                             },
                                                             forge::db::authenticated::proof_tree::changes);
      require(changes.total_size == expected.change_count && changes.items.size() == 1U &&
                  changes.items.front().key == first_change_key,
              "authenticated benchmark historical change root is inconsistent");
      ++result.retention_content_checks;
      ++result.verified_retained_versions;
   }
   result.retention_verification = clock_type::now() - started;
}

boost::asio::awaitable<benchmark_result> run_benchmark(const options& settings,
                                                       const std::shared_ptr<forge::db::mdbx::driver>& driver) {
   auto authenticated = forge::db::authenticated::store{
       driver,
       {
           .family = forge::db::core::family{"authenticated"},
           .domain = "forge.benchmark.db.authenticated.v1",
       },
   };
   auto result = benchmark_result{};

   const auto initial_load_started = clock_type::now();
   auto first = std::size_t{};
   auto version = forge::db::authenticated::version_id_t{};
   while (first < settings.keys) {
      const auto count = std::min(settings.load_chunk_keys, settings.keys - first);
      const auto mutation_construction_started = clock_type::now();
      const auto mutations = make_mutations(settings, first, count);
      result.mutation_construction += clock_type::now() - mutation_construction_started;
      result.root = co_await commit_database_chunk(authenticated, driver, version, mutations, result.initial_batch);
      first += count;
      ++version;
      ++result.committed_versions;
   }
   result.initial_load_wall = clock_type::now() - initial_load_started;
   const auto planned_versions = committed_version_count(settings.keys, settings.load_chunk_keys);
   require(result.initial_batch + result.mutation_construction <= result.initial_load_wall,
           "initial batch DB and mutation construction timers overlap");
   require(result.root.state_size == settings.keys, "chunked load did not commit every key");
   require(result.committed_versions == planned_versions, "initial load committed an unexpected number of versions");
   require(result.root.version + 1U == result.committed_versions,
           "initial load root version does not match the committed version count");

   for (auto sample = std::size_t{}; sample < point_proof_count; ++sample) {
      const auto key = make_key(sample_position(sample, point_proof_count, settings.keys));
      const auto started = clock_type::now();
      const auto proof = co_await authenticated.prove(result.root.version, key, false);
      result.point_proofs.elapsed += clock_type::now() - started;
      require(proof.anchor == result.root, "point proof is not bound to the committed root");
      require(proof.terminal && proof.terminal->key == key, "point proof does not contain the requested key");
      result.point_proofs.wire_bytes += forge::db::authenticated::wire_size(proof);
   }

   const auto maximum_range_start = settings.keys > range_proof_limit ? settings.keys - range_proof_limit : 0U;
   for (auto sample = std::size_t{}; sample < range_proof_count; ++sample) {
      const auto lower = make_key(inclusive_sample_position(sample, range_proof_count, maximum_range_start));
      const auto request = forge::db::authenticated::range_request{
          .lower = lower,
          .limit = range_proof_limit,
          .include_values = false,
      };
      const auto started = clock_type::now();
      const auto proof = co_await authenticated.prove_range(result.root.version, request);
      result.range_proofs.elapsed += clock_type::now() - started;
      require(proof.anchor == result.root, "range proof is not bound to the committed root");
      require(proof.request == request, "range proof does not preserve the requested bounds");
      result.range_proofs.wire_bytes += forge::db::authenticated::wire_size(proof);
      result.range_proofs.nodes += proof.nodes.size();
   }

   co_await verify_retained_versions(settings, authenticated, result);
   require(result.verified_retained_versions == planned_versions,
           "initial load did not retain every committed version");

   co_return result;
}

double milliseconds(std::chrono::nanoseconds elapsed) {
   return std::chrono::duration<double, std::milli>{elapsed}.count();
}

double operations_per_second(std::size_t operations, std::chrono::nanoseconds elapsed) {
   const auto seconds = std::chrono::duration<double>{elapsed}.count();
   return seconds == 0.0 ? 0.0 : static_cast<double>(operations) / seconds;
}

std::string json_escape(std::string_view value) {
   constexpr auto hex = std::string_view{"0123456789abcdef"};
   auto result = std::string{};
   result.reserve(value.size());
   for (const auto character : value) {
      const auto byte = static_cast<unsigned char>(character);
      switch (character) {
      case '"':
         result += "\\\"";
         break;
      case '\\':
         result += "\\\\";
         break;
      case '\b':
         result += "\\b";
         break;
      case '\f':
         result += "\\f";
         break;
      case '\n':
         result += "\\n";
         break;
      case '\r':
         result += "\\r";
         break;
      case '\t':
         result += "\\t";
         break;
      default:
         if (byte < 0x20U) {
            result += "\\u00";
            result += hex[(byte >> 4U) & 0x0fU];
            result += hex[byte & 0x0fU];
         } else {
            result += character;
         }
      }
   }
   return result;
}

bool print_result(const options& settings, const std::filesystem::path& path, const benchmark_result& result,
                  const database_footprint& footprint) {
   const auto initial_batch_rate = operations_per_second(settings.keys, result.initial_batch);
   const auto point_proof_rate = operations_per_second(point_proof_count, result.point_proofs.elapsed);
   const auto range_proof_rate = operations_per_second(range_proof_count, result.range_proofs.elapsed);
   const auto thresholds = provisional_thresholds(settings.baseline);
   const auto initial_batch_passed = !thresholds || initial_batch_rate >= thresholds->initial_batch_min_keys_per_second;
   const auto point_proofs_passed = !thresholds || point_proof_rate >= thresholds->point_proofs_min_per_second;
   const auto range_proofs_passed = !thresholds || range_proof_rate >= thresholds->range_proofs_min_per_second;
   const auto gate_passed = initial_batch_passed && point_proofs_passed && range_proofs_passed;

   std::cout << std::fixed << std::setprecision(3) << "{\n"
             << "  \"format\": \"forge.db.authenticated.benchmark.v3\",\n"
             << "  \"benchmark\": \"forge_db_authenticated\",\n"
             << "  \"config\": {\n"
             << "    \"workload\": \"initial_bulk_state\",\n"
             << "    \"measurement_scope\": \"transaction_join_stage_durable_commit\",\n"
             << "    \"baseline\": \"" << baseline_name(settings.baseline) << "\",\n"
             << "    \"machine_label\": \"" << json_escape(settings.machine_label) << "\",\n"
             << "    \"keys\": " << settings.keys << ",\n"
             << "    \"value_bytes\": " << settings.value_bytes << ",\n"
             << "    \"load_chunk_keys\": " << settings.load_chunk_keys << ",\n"
             << "    \"load_chunk_keys_source\": \"" << chunk_keys_source(settings) << "\",\n"
             << "    \"planned_committed_versions\": "
             << committed_version_count(settings.keys, settings.load_chunk_keys) << ",\n"
             << "    \"path\": \"" << json_escape(path.string()) << "\",\n"
             << "    \"mdbx_upper_bytes\": " << settings.mdbx_upper_bytes << ",\n"
             << "    \"mdbx_growth_bytes\": " << mdbx_growth_step(settings.keys) << ",\n"
             << "    \"point_proof_count\": " << point_proof_count << ",\n"
             << "    \"range_proof_count\": " << range_proof_count << ",\n"
             << "    \"range_proof_limit\": " << range_proof_limit << "\n"
             << "  },\n"
             << "  \"root\": {\n"
             << "    \"version\": " << result.root.version << ",\n"
             << "    \"state_root\": \"" << result.root.state_root.str() << "\",\n"
             << "    \"state_size\": " << result.root.state_size << ",\n"
             << "    \"change_root\": \"" << result.root.change_root.str() << "\",\n"
             << "    \"change_count\": " << result.root.change_count << "\n"
             << "  },\n"
             << "  \"storage\": {\n"
             << "    \"committed_versions\": " << result.committed_versions << ",\n"
             << "    \"retained_versions\": " << result.verified_retained_versions << ",\n"
             << "    \"retention_content_checks\": " << result.retention_content_checks << ",\n"
             << "    \"retention_verification_elapsed_ms\": " << milliseconds(result.retention_verification) << ",\n"
             << "    \"database_logical_bytes\": " << footprint.logical_bytes << ",\n"
             << "    \"database_file_count\": " << footprint.files << "\n"
             << "  },\n"
             << "  \"metrics\": {\n"
             << "    \"initial_batch\": {\n"
             << "      \"elapsed_ms\": " << milliseconds(result.initial_batch) << ",\n"
             << "      \"keys_per_second\": " << initial_batch_rate << ",\n"
             << "      \"load_wall_elapsed_ms\": " << milliseconds(result.initial_load_wall) << ",\n"
             << "      \"excluded_mutation_construction_elapsed_ms\": " << milliseconds(result.mutation_construction)
             << "\n"
             << "    },\n"
             << "    \"point_proofs\": {\n"
             << "      \"elapsed_ms\": " << milliseconds(result.point_proofs.elapsed) << ",\n"
             << "      \"proofs_per_second\": " << point_proof_rate << ",\n"
             << "      \"average_wire_bytes\": "
             << static_cast<double>(result.point_proofs.wire_bytes) / point_proof_count << "\n"
             << "    },\n"
             << "    \"range_proofs\": {\n"
             << "      \"elapsed_ms\": " << milliseconds(result.range_proofs.elapsed) << ",\n"
             << "      \"proofs_per_second\": " << range_proof_rate << ",\n"
             << "      \"average_wire_bytes\": "
             << static_cast<double>(result.range_proofs.wire_bytes) / range_proof_count << ",\n"
             << "      \"average_nodes\": " << static_cast<double>(result.range_proofs.nodes) / range_proof_count
             << "\n"
             << "    }\n"
             << "  },\n"
             << "  \"acceptance\": {\n"
             << "    \"classification\": \"provisional_measurement_gate\",\n"
             << "    \"latency_slo\": false,\n"
             << "    \"status\": \"" << (thresholds ? (gate_passed ? "pass" : "fail") : "not_evaluated") << "\",\n";
   if (thresholds) {
      std::cout << "    \"thresholds\": {\n"
                << "      \"initial_batch_min_keys_per_second\": " << thresholds->initial_batch_min_keys_per_second
                << ",\n"
                << "      \"point_proofs_min_per_second\": " << thresholds->point_proofs_min_per_second << ",\n"
                << "      \"range_proofs_min_per_second\": " << thresholds->range_proofs_min_per_second << "\n"
                << "    },\n"
                << "    \"checks\": {\n"
                << "      \"initial_batch\": " << (initial_batch_passed ? "true" : "false") << ",\n"
                << "      \"point_proofs\": " << (point_proofs_passed ? "true" : "false") << ",\n"
                << "      \"range_proofs\": " << (range_proofs_passed ? "true" : "false") << "\n"
                << "    }\n";
   } else {
      std::cout << "    \"thresholds\": null,\n"
                << "    \"checks\": null\n";
   }
   std::cout << "  }\n"
             << "}\n";
   return gate_passed;
}

} // namespace

int main(int argc, char** argv) try {
   auto arguments = std::vector<std::string_view>{};
   arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
   for (auto index = 1; index < argc; ++index) {
      arguments.emplace_back(argv[index]);
   }
   const auto settings = parse_options(arguments);
   if (settings.help) {
      std::cout << usage << '\n';
      return 0;
   }
   if (settings.value_bytes > forge::db::authenticated::limits{}.max_value_bytes) {
      usage_error("--value-bytes exceeds the authenticated-store limit");
   }
   const auto temporary_path = settings.path.empty();
   const auto path =
       temporary_path
           ? std::filesystem::temp_directory_path() /
                 ("forge_db_authenticated_benchmark_" + std::to_string(clock_type::now().time_since_epoch().count()))
           : std::filesystem::absolute(settings.path);
   if (std::filesystem::exists(path)) {
      usage_error("--path must name a path that does not exist");
   }
   if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
   }
   [[maybe_unused]] auto path_guard = temporary_path_guard{path, temporary_path};
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{{.thread_name = "authenticated-benchmark"}};
   auto result = benchmark_result{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await forge::db::mdbx::driver::open(
          forge::db::mdbx::config{
              .path = path.string(),
              .families = {"authenticated"},
              .map =
                  {
                      .upper_size = settings.mdbx_upper_bytes,
                      .growth_step = mdbx_growth_step(settings.keys),
                  },
          },
          lane.get_executor());
      result = co_await run_benchmark(settings, driver);
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   const auto footprint = measure_database_footprint(path);
   const auto gate_passed = print_result(settings, path, result, footprint);
   return gate_passed ? 0 : 2;
} catch (const std::exception& error) {
   std::cerr << "benchmark failed: " << error.what() << '\n';
   return 1;
}
