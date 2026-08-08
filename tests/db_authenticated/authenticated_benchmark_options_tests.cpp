#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "authenticated_benchmark_options.hpp"

namespace benchmark = forge::test::db_authenticated::benchmark;

namespace {

struct test_root {
   std::uint64_t version = 0;
   std::uint64_t state_size = 0;
   std::uint64_t change_count = 0;
};

} // namespace

BOOST_AUTO_TEST_CASE(authenticated_benchmark_profile_defaults_bound_committed_versions) {
   constexpr auto one_million_arguments = std::array{std::string_view{"--baseline"}, std::string_view{"1m"}};
   const auto one_million = benchmark::parse_options(one_million_arguments);
   BOOST_TEST(one_million.keys == 1'000'000U);
   BOOST_TEST(one_million.load_chunk_keys == 32'768U);
   BOOST_TEST(one_million.mdbx_upper_bytes == benchmark::default_mdbx_upper_bytes);
   BOOST_TEST(!one_million.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(one_million) == "profile_default");
   BOOST_TEST(benchmark::committed_version_count(one_million.keys, one_million.load_chunk_keys) == 31U);

   constexpr auto ten_million_arguments = std::array{std::string_view{"--baseline=10m"}};
   const auto ten_million = benchmark::parse_options(ten_million_arguments);
   BOOST_TEST(ten_million.keys == 10'000'000U);
   BOOST_TEST(ten_million.load_chunk_keys == 65'536U);
   BOOST_TEST(ten_million.mdbx_upper_bytes == benchmark::default_mdbx_upper_bytes);
   BOOST_TEST(!ten_million.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(ten_million) == "profile_default");
   BOOST_TEST(benchmark::committed_version_count(ten_million.keys, ten_million.load_chunk_keys) == 153U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_mdbx_upper_override_preserves_profile_defaults) {
   constexpr auto one_million_arguments =
       std::array{std::string_view{"--baseline=1m"}, std::string_view{"--mdbx-upper-bytes=8589934592"}};
   const auto one_million = benchmark::parse_options(one_million_arguments);
   BOOST_TEST(one_million.mdbx_upper_bytes == 8'589'934'592ULL);
   BOOST_TEST(one_million.keys == 1'000'000U);
   BOOST_TEST(one_million.load_chunk_keys == 32'768U);

   constexpr auto ten_million_arguments =
       std::array{std::string_view{"--mdbx-upper-bytes=68719476736"}, std::string_view{"--baseline=10m"}};
   const auto ten_million = benchmark::parse_options(ten_million_arguments);
   BOOST_TEST(ten_million.mdbx_upper_bytes == 68'719'476'736ULL);
   BOOST_TEST(ten_million.keys == 10'000'000U);
   BOOST_TEST(ten_million.load_chunk_keys == 65'536U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_chunk_override_is_order_independent) {
   constexpr auto before_baseline = std::array{std::string_view{"--chunk-keys"}, std::string_view{"4096"},
                                               std::string_view{"--baseline"}, std::string_view{"10m"}};
   const auto before = benchmark::parse_options(before_baseline);
   BOOST_TEST(before.load_chunk_keys == 4'096U);
   BOOST_TEST(before.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(before) == "override");
   BOOST_TEST(benchmark::committed_version_count(before.keys, before.load_chunk_keys) == 2'442U);

   constexpr auto after_baseline =
       std::array{std::string_view{"--baseline=1m"}, std::string_view{"--chunk-keys=125000"}};
   const auto after = benchmark::parse_options(after_baseline);
   BOOST_TEST(after.load_chunk_keys == 125'000U);
   BOOST_TEST(after.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(after) == "override");
   BOOST_TEST(benchmark::committed_version_count(after.keys, after.load_chunk_keys) == 8U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_custom_profile_keeps_bounded_default) {
   constexpr auto arguments = std::array{std::string_view{"--keys"}, std::string_view{"10000"}};
   const auto settings = benchmark::parse_options(arguments);
   BOOST_CHECK(settings.baseline == benchmark::baseline_profile::custom);
   BOOST_TEST(settings.load_chunk_keys == 4'096U);
   BOOST_TEST(benchmark::chunk_keys_source(settings) == "custom_default");
   BOOST_TEST(benchmark::committed_version_count(settings.keys, settings.load_chunk_keys) == 3U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_retained_version_expectations_cover_scaled_tail) {
   const auto one_million_tail = benchmark::expected_retained_version(30U, 1'000'000U, 32'768U);
   BOOST_TEST(one_million_tail.version == 30U);
   BOOST_TEST(one_million_tail.state_size == 1'000'000U);
   BOOST_TEST(one_million_tail.change_count == 16'960U);

   const auto ten_million_tail = benchmark::expected_retained_version(152U, 10'000'000U, 65'536U);
   BOOST_TEST(ten_million_tail.version == 152U);
   BOOST_TEST(ten_million_tail.state_size == 10'000'000U);
   BOOST_TEST(ten_million_tail.change_count == 38'528U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_retained_root_validation_rejects_loss_and_mismatch) {
   const auto expected = benchmark::expected_retained_version(1U, 10U, 4U);
   const auto valid = std::optional{test_root{.version = 1U, .state_size = 8U, .change_count = 4U}};
   BOOST_CHECK_NO_THROW(benchmark::validate_retained_root(valid, expected));
   BOOST_CHECK_THROW(benchmark::validate_retained_root(std::optional<test_root>{}, expected), std::runtime_error);
   BOOST_CHECK_THROW(benchmark::validate_retained_root(
                         std::optional{test_root{.version = 0U, .state_size = 8U, .change_count = 4U}}, expected),
                     std::runtime_error);
   BOOST_CHECK_THROW(benchmark::validate_retained_root(
                         std::optional{test_root{.version = 1U, .state_size = 7U, .change_count = 4U}}, expected),
                     std::runtime_error);
   BOOST_CHECK_THROW(benchmark::validate_retained_root(
                         std::optional{test_root{.version = 1U, .state_size = 8U, .change_count = 3U}}, expected),
                     std::runtime_error);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_rejects_conflicting_or_invalid_cli_options) {
   constexpr auto conflicting = std::array{std::string_view{"--baseline=1m"}, std::string_view{"--keys=10"}};
   BOOST_CHECK_THROW(benchmark::parse_options(conflicting), std::invalid_argument);

   constexpr auto zero_chunk = std::array{std::string_view{"--chunk-keys=0"}};
   BOOST_CHECK_THROW(benchmark::parse_options(zero_chunk), std::invalid_argument);

   constexpr auto zero_mdbx_upper = std::array{std::string_view{"--mdbx-upper-bytes=0"}};
   BOOST_CHECK_THROW(benchmark::parse_options(zero_mdbx_upper), std::invalid_argument);

   constexpr auto unknown = std::array{std::string_view{"--unexpected"}};
   BOOST_CHECK_THROW(benchmark::parse_options(unknown), std::invalid_argument);
}
