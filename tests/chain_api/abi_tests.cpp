#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import forge.chain.api.abi;
import forge.raw.raw;

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

forge::variant object(std::initializer_list<std::pair<std::string, forge::variant>> fields) {
   auto value = forge::mutable_variant_object{};
   value.reserve(fields.size());
   for (const auto& [name, field] : fields) {
      value.set(name, field);
   }
   return forge::variant{std::move(value)};
}

template <typename... Values> forge::variant array(Values&&... values) {
   auto result = forge::variants{};
   result.reserve(sizeof...(Values));
   (result.emplace_back(std::forward<Values>(values)), ...);
   return forge::variant{std::move(result)};
}

protocol::abi_def empty_abi(std::string version = "eosio::abi/1.2") {
   auto abi = protocol::abi_def{};
   abi.version = std::move(version);
   return abi;
}

protocol::abi_def spring_shape_abi() {
   auto abi = empty_abi();
   abi.types = {
       protocol::type_def{.new_type_name = "small", .type = "uint8"},
   };
   abi.structs = {
       protocol::struct_def{
           .name = "base",
           .fields =
               {
                   protocol::field_def{.name = "base_value", .type = "uint16"},
               },
       },
       protocol::struct_def{
           .name = "record",
           .base = "base",
           .fields =
               {
                   protocol::field_def{.name = "fixed", .type = "small[3]"},
                   protocol::field_def{.name = "maybe", .type = "uint32?"},
                   protocol::field_def{.name = "tail", .type = "string$"},
               },
       },
       protocol::struct_def{
           .name = "extension_record",
           .fields =
               {
                   protocol::field_def{.name = "i0", .type = "int8"},
                   protocol::field_def{.name = "i1", .type = "int8"},
                   protocol::field_def{.name = "i2", .type = "int8$"},
                   protocol::field_def{.name = "a", .type = "int8[]$"},
                   protocol::field_def{.name = "o", .type = "int8?$"},
                   protocol::field_def{.name = "fa", .type = "int8[2]$"},
               },
       },
   };
   abi.variants.value = {
       protocol::variant_def{.name = "v1", .types = {"int8", "string", "int16"}},
   };
   return abi;
}

void require_diagnostic(const chain_api::abi_serialization_error& error, chain_api::abi_error_code code,
                        std::string_view type, std::string_view path, std::size_t offset) {
   const auto& diagnostic = error.diagnostic();
   BOOST_TEST(static_cast<int>(diagnostic.code) == static_cast<int>(code));
   BOOST_TEST(diagnostic.type == type);
   BOOST_TEST(diagnostic.path == path);
   BOOST_TEST(diagnostic.offset == offset);
   BOOST_TEST(!diagnostic.message.empty());
}

forge::variant recursive_node(std::size_t depth) {
   auto next = forge::variant{};
   for (auto index = depth; index > 0U; --index) {
      next = object({
          {"value", forge::variant{static_cast<std::uint64_t>(index)}},
          {"next", std::move(next)},
      });
   }
   return next;
}

} // namespace

BOOST_AUTO_TEST_CASE(chain_abi_spring_variant_goldens) {
   const auto abi = spring_shape_abi();

   const auto int8_binary = chain_api::abi_json_to_bin(abi, "v1", array("int8", 21));
   BOOST_TEST(int8_binary == protocol::bytes({0x00, 0x15}));
   BOOST_CHECK(chain_api::abi_bin_to_json(abi, "v1", int8_binary) == array("int8", 21));

   const auto string_binary = chain_api::abi_json_to_bin(abi, "v1", array("string", "abcd"));
   BOOST_TEST(string_binary == protocol::bytes({0x01, 0x04, 0x61, 0x62, 0x63, 0x64}));
   BOOST_CHECK(chain_api::abi_bin_to_json(abi, "v1", string_binary) == array("string", "abcd"));

   const auto int16_binary = chain_api::abi_json_to_bin(abi, "v1", array("int16", 3));
   BOOST_TEST(int16_binary == protocol::bytes({0x02, 0x03, 0x00}));
}

BOOST_AUTO_TEST_CASE(chain_abi_renders_spring_transaction_actions_with_data_and_hex_fallback) {
   auto abi = empty_abi();
   abi.structs = {
       protocol::struct_def{
           .name = "ping",
           .fields = {protocol::field_def{.name = "value", .type = "uint32"}},
       },
   };
   abi.actions = {
       protocol::action_def{.name = protocol::action_name{"ping"}, .type = "ping"},
   };

   auto action = protocol::action{};
   action.account = protocol::account_name{"tester"};
   action.name = protocol::action_name{"ping"};
   action.authorization = {
       protocol::permission_level{.actor = protocol::account_name{"tester"},
                                  .permission = protocol::permission_name{"active"}},
   };
   action.data = chain_api::abi_json_to_bin(abi, "ping", object({{"value", 7}}));

   auto transaction = protocol::transaction{};
   transaction.actions.push_back(action);
   const auto resolver = [abi](protocol::account_name account) -> std::optional<protocol::abi_def> {
      return account == protocol::account_name{"tester"} ? std::optional{abi} : std::nullopt;
   };

   const auto rendered = chain_api::transaction_to_variant(transaction, resolver);
   const auto& rendered_action = rendered["actions"][std::size_t{0U}];
   BOOST_CHECK(rendered_action["data"] == object({{"value", 7}}));
   BOOST_TEST(rendered_action["hex_data"].as_string() == "07000000");

   const auto raw =
       chain_api::action_to_variant(action, [](protocol::account_name) { return std::optional<protocol::abi_def>{}; });
   BOOST_TEST(raw["data"].as_string() == "07000000");
   BOOST_TEST(raw["hex_data"].as_string() == "07000000");

   transaction.transaction_extensions.emplace_back(9U, protocol::bytes{});
   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::transaction_to_variant(transaction, resolver)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::invalid_binary &&
                                   error.diagnostic().path == "transaction_extensions";
                         });
}

BOOST_AUTO_TEST_CASE(chain_abi_translates_resolver_failures_to_typed_diagnostics) {
   auto action = protocol::action{};
   action.account = protocol::account_name{"tester"};
   action.name = protocol::action_name{"ping"};

   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::action_to_variant(action,
                                                      [](protocol::account_name) -> std::optional<protocol::abi_def> {
                                                         throw std::runtime_error{"resolver unavailable"};
                                                      })),
       chain_api::abi_serialization_error, [](const auto& error) {
          return error.diagnostic().code == chain_api::abi_error_code::invalid_abi &&
                 error.diagnostic().path == "tester" &&
                 error.diagnostic().message.find("resolver unavailable") != std::string::npos;
       });
}

BOOST_AUTO_TEST_CASE(chain_abi_rejects_oversized_action_authorization_before_resolver) {
   auto action = protocol::action{};
   action.authorization.resize(2U);
   action.data = {0x00, 0x01};

   auto limits = chain_api::abi_serialization_limits{};
   limits.max_container_elements = 1U;
   limits.max_binary_bytes = 1U;
   auto resolver_calls = std::size_t{};
   const auto resolver = [&resolver_calls](protocol::account_name) {
      ++resolver_calls;
      return std::optional<protocol::abi_def>{};
   };

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::action_to_variant(action, resolver, limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().type == "action" && error.diagnostic().path == "authorization";
                         });
   BOOST_TEST(resolver_calls == 0U);
}

BOOST_AUTO_TEST_CASE(chain_abi_transaction_preflights_each_action_authorization_before_resolver) {
   for (const auto context_free : {true, false}) {
      auto action = protocol::action{};
      action.authorization.resize(3U);
      auto transaction = protocol::transaction{};
      if (context_free) {
         transaction.context_free_actions.emplace_back();
         transaction.context_free_actions.push_back(std::move(action));
      } else {
         transaction.actions.emplace_back();
         transaction.actions.push_back(std::move(action));
      }

      auto limits = chain_api::abi_serialization_limits{};
      limits.max_container_elements = 2U;
      auto resolver_calls = std::size_t{};
      const auto resolver = [&resolver_calls](protocol::account_name) {
         ++resolver_calls;
         return std::optional<protocol::abi_def>{};
      };

      const auto* context = context_free ? "context-free action" : "ordinary action";
      BOOST_TEST_CONTEXT(context) {
         BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::transaction_to_variant(transaction, resolver, limits)),
                               chain_api::abi_serialization_error, [](const auto& error) {
                                  return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                         error.diagnostic().type == "action" &&
                                         error.diagnostic().path == "authorization";
                               });
         BOOST_TEST(resolver_calls == 0U);
      }
   }
}

BOOST_AUTO_TEST_CASE(chain_abi_rejects_oversized_action_data_before_no_abi_fallback) {
   auto action = protocol::action{};
   action.data = {0x00, 0x01};

   auto limits = chain_api::abi_serialization_limits{};
   limits.max_binary_bytes = 1U;
   auto resolver_calls = std::size_t{};
   const auto resolver = [&resolver_calls](protocol::account_name) {
      ++resolver_calls;
      return std::optional<protocol::abi_def>{};
   };

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::action_to_variant(action, resolver, limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().type == "action" && error.diagnostic().path == "data";
                         });
   BOOST_TEST(resolver_calls == 0U);
}

BOOST_AUTO_TEST_CASE(chain_abi_transaction_rejects_oversized_action_data_before_matching_resolver) {
   auto abi = empty_abi();
   abi.actions = {
       protocol::action_def{.name = protocol::action_name{"payload"}, .type = "bytes"},
   };

   auto action = protocol::action{};
   action.account = protocol::account_name{"tester"};
   action.name = protocol::action_name{"payload"};
   action.data = {0x00, 0x01};
   auto transaction = protocol::transaction{};
   transaction.actions.push_back(action);

   auto limits = chain_api::abi_serialization_limits{};
   limits.max_binary_bytes = 1U;
   auto resolver_calls = std::size_t{};
   const auto resolver = [&abi, &resolver_calls](protocol::account_name account) -> std::optional<protocol::abi_def> {
      ++resolver_calls;
      return account == protocol::account_name{"tester"} ? std::optional{abi} : std::nullopt;
   };

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::transaction_to_variant(transaction, resolver, limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().type == "action" && error.diagnostic().path == "data";
                         });
   BOOST_TEST(resolver_calls == 0U);
}

BOOST_AUTO_TEST_CASE(chain_abi_does_not_fallback_on_matching_abi_size_limit) {
   auto abi = empty_abi();
   abi.actions = {
       protocol::action_def{.name = protocol::action_name{"payload"}, .type = "bytes"},
   };

   auto action = protocol::action{};
   action.account = protocol::account_name{"tester"};
   action.name = protocol::action_name{"payload"};
   action.data = {0x02, 0xaa, 0xbb};

   auto limits = chain_api::abi_serialization_limits{};
   limits.max_binary_bytes = action.data.size();
   limits.max_string_bytes = 1U;
   auto resolver_calls = std::size_t{};
   const auto resolver = [&abi, &resolver_calls](protocol::account_name account) -> std::optional<protocol::abi_def> {
      ++resolver_calls;
      return account == protocol::account_name{"tester"} ? std::optional{abi} : std::nullopt;
   };

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::action_to_variant(action, resolver, limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().type == "bytes" && error.diagnostic().path == "bytes";
                         });
   BOOST_TEST(resolver_calls == 1U);
}

BOOST_AUTO_TEST_CASE(chain_abi_preserves_hex_fallback_for_matching_abi_decode_incompatibility) {
   auto abi = empty_abi();
   abi.actions = {
       protocol::action_def{.name = protocol::action_name{"payload"}, .type = "uint32"},
   };

   auto action = protocol::action{};
   action.account = protocol::account_name{"tester"};
   action.name = protocol::action_name{"payload"};
   action.data = {0x07};
   const auto resolver = [&abi](protocol::account_name account) -> std::optional<protocol::abi_def> {
      return account == protocol::account_name{"tester"} ? std::optional{abi} : std::nullopt;
   };

   const auto rendered = chain_api::action_to_variant(action, resolver);
   BOOST_TEST(rendered["data"].as_string() == "07");
   BOOST_TEST(rendered["hex_data"].as_string() == "07");
}

BOOST_AUTO_TEST_CASE(chain_abi_rejects_oversized_context_free_actions_before_resolver) {
   auto transaction = protocol::transaction{};
   transaction.context_free_actions.resize(2U);

   auto limits = chain_api::abi_serialization_limits{};
   limits.max_container_elements = 1U;
   auto resolver_calls = std::size_t{};
   const auto resolver = [&resolver_calls](protocol::account_name) {
      ++resolver_calls;
      return std::optional<protocol::abi_def>{};
   };

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::transaction_to_variant(transaction, resolver, limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().path == "context_free_actions";
                         });
   BOOST_TEST(resolver_calls == 0U);
}

BOOST_AUTO_TEST_CASE(chain_abi_rejects_oversized_actions_before_resolver) {
   auto transaction = protocol::transaction{};
   transaction.context_free_actions.resize(1U);
   transaction.actions.resize(2U);

   auto limits = chain_api::abi_serialization_limits{};
   limits.max_container_elements = 1U;
   auto resolver_calls = std::size_t{};
   const auto resolver = [&resolver_calls](protocol::account_name) {
      ++resolver_calls;
      return std::optional<protocol::abi_def>{};
   };

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::transaction_to_variant(transaction, resolver, limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().path == "actions";
                         });
   BOOST_TEST(resolver_calls == 0U);
}

BOOST_AUTO_TEST_CASE(chain_abi_spring_binary_extension_goldens) {
   const auto abi = spring_shape_abi();

   const auto prefix = object({{"i0", 5}, {"i1", 6}});
   BOOST_TEST(chain_api::abi_json_to_bin(abi, "extension_record", prefix) == protocol::bytes({0x05, 0x06}));

   const auto with_array = object({
       {"i0", 5},
       {"i1", 6},
       {"i2", 7},
       {"a", array(8, 9, 10)},
   });
   const auto array_binary = chain_api::abi_json_to_bin(abi, "extension_record", with_array);
   BOOST_TEST(array_binary == protocol::bytes({0x05, 0x06, 0x07, 0x03, 0x08, 0x09, 0x0a}));

   const auto complete = object({
       {"i0", 5},
       {"i1", 6},
       {"i2", 7},
       {"a", array(8, 9, 10)},
       {"o", 31},
       {"fa", array(1, 2)},
   });
   const auto complete_binary = chain_api::abi_json_to_bin(abi, "extension_record", complete);
   BOOST_TEST(complete_binary == protocol::bytes({0x05, 0x06, 0x07, 0x03, 0x08, 0x09, 0x0a, 0x01, 0x1f, 0x01, 0x02}));
   BOOST_TEST(chain_api::abi_json_to_bin(abi, "extension_record",
                                         chain_api::abi_bin_to_json(abi, "extension_record", complete_binary)) ==
              complete_binary);
}

BOOST_AUTO_TEST_CASE(chain_abi_struct_inheritance_arrays_optional_and_aliases) {
   const auto abi = spring_shape_abi();
   const auto value = object({
       {"base_value", 0x1234},
       {"fixed", array(1, 2, 3)},
       {"maybe", 0x01020304},
       {"tail", "ok"},
   });

   const auto binary = chain_api::abi_json_to_bin(abi, "record", value);
   BOOST_TEST(binary ==
              protocol::bytes({0x34, 0x12, 0x01, 0x02, 0x03, 0x01, 0x04, 0x03, 0x02, 0x01, 0x02, 0x6f, 0x6b}));
   BOOST_TEST(chain_api::abi_json_to_bin(abi, "record", chain_api::abi_bin_to_json(abi, "record", binary)) == binary);

   const auto without_tail = object({
       {"base_value", 0x1234},
       {"fixed", array(1, 2, 3)},
       {"maybe", forge::variant{}},
   });
   const auto prefix = chain_api::abi_json_to_bin(abi, "record", without_tail);
   const auto decoded = chain_api::abi_bin_to_json(abi, "record", prefix);
   BOOST_TEST(decoded.get_object().contains("maybe"));
   BOOST_TEST(decoded["maybe"].is_null());
   BOOST_TEST(!decoded.get_object().contains("tail"));
}

BOOST_AUTO_TEST_CASE(chain_abi_donor_builtins_round_trip) {
   const auto abi = empty_abi();
   const auto zero160 = std::string(40, '0');
   const auto zero256 = std::string(64, '0');
   const auto zero512 = std::string(128, '0');
   const auto public_key = std::string{"EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"};
   const auto signature = std::string{
       "SIG_K1_Jzdpi5RCzHLGsQbpGhndXBzcFs8vT5LHAtWLMxPzBdwRHSmJkcCdVu6oqPUQn1hbGUdErHvxtdSTS1YA73BThQFwV1v4G5"};

   const auto values = std::vector<std::pair<std::string, forge::variant>>{
       {"bool", true},
       {"int8", -7},
       {"uint8", 7},
       {"int16", -300},
       {"uint16", 300},
       {"int32", -70'000},
       {"uint32", 70'000},
       {"int64", -9'000'000},
       {"uint64", 9'000'000},
       {"int128", -42},
       {"uint128", "42"},
       {"varint32", -300},
       {"varuint32", 300},
       {"float32", 1.5},
       {"float64", -3.25},
       {"float128", "0x000102030405060708090a0b0c0d0e0f"},
       {"time_point", "2000-01-01T00:00:00"},
       {"time_point_sec", "2000-01-01T00:00:00"},
       {"block_timestamp_type", "2000-01-01T00:00:00"},
       {"name", "alice"},
       {"bytes", "00a5ff"},
       {"string", "spring fixture"},
       {"checksum160", zero160},
       {"checksum256", zero256},
       {"checksum512", zero512},
       {"public_key", public_key},
       {"signature", signature},
       {"symbol", "4,SYS"},
       {"symbol_code", "SYS"},
       {"asset", "100.0000 SYS"},
       {"extended_asset", object({{"quantity", "1.0000 SYS"}, {"contract", "eosio.token"}})},
   };

   for (const auto& [type, value] : values) {
      const auto binary = chain_api::abi_json_to_bin(abi, type, value);
      const auto decoded = chain_api::abi_bin_to_json(abi, type, binary);
      BOOST_TEST_CONTEXT("built-in " << type) {
         BOOST_TEST(chain_api::abi_json_to_bin(abi, type, decoded) == binary);
      }
   }
}

BOOST_AUTO_TEST_CASE(chain_abi_reports_exact_missing_and_trailing_diagnostics) {
   auto abi = empty_abi();
   abi.structs = {
       protocol::struct_def{
           .name = "record",
           .fields =
               {
                   protocol::field_def{.name = "first", .type = "uint8"},
                   protocol::field_def{.name = "required", .type = "uint16"},
               },
       },
   };

   try {
      (void)chain_api::abi_json_to_bin(abi, "record", object({{"first", 7}}));
      BOOST_FAIL("missing ABI field was accepted");
   } catch (const chain_api::abi_serialization_error& error) {
      require_diagnostic(error, chain_api::abi_error_code::missing_field, "uint16", "record.required", 1U);
      BOOST_TEST(error.diagnostic().message == "Missing field in ABI JSON object");
   }

   try {
      (void)chain_api::abi_bin_to_json(abi, "uint8", protocol::bytes{0x07, 0x08});
      BOOST_FAIL("trailing ABI bytes were accepted");
   } catch (const chain_api::abi_serialization_error& error) {
      require_diagnostic(error, chain_api::abi_error_code::trailing_bytes, "uint8", "uint8", 1U);
      BOOST_TEST(error.diagnostic().message == "ABI binary contains trailing bytes");
   }

   try {
      (void)chain_api::abi_bin_to_json(abi, "string", protocol::bytes{0x03, 0x61});
      BOOST_FAIL("truncated ABI string was accepted");
   } catch (const chain_api::abi_serialization_error& error) {
      require_diagnostic(error, chain_api::abi_error_code::invalid_binary, "string", "string", 1U);
      BOOST_TEST(error.diagnostic().message == "ABI binary ended inside a string");
   }
}

BOOST_AUTO_TEST_CASE(chain_abi_enforces_recursion_deadline_and_size_limits) {
   auto recursive_abi = empty_abi();
   recursive_abi.structs = {
       protocol::struct_def{
           .name = "node",
           .fields =
               {
                   protocol::field_def{.name = "value", .type = "uint8"},
                   protocol::field_def{.name = "next", .type = "node?"},
               },
       },
   };

   auto depth_limits = chain_api::abi_serialization_limits{};
   depth_limits.max_recursion_depth = 5;
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::abi_json_to_bin(recursive_abi, "node", recursive_node(6), depth_limits)),
       chain_api::abi_serialization_error, [](const auto& error) {
          return error.diagnostic().code == chain_api::abi_error_code::recursion_limit &&
                 error.diagnostic().path.starts_with("node.next");
       });

   auto deadline_limits = chain_api::abi_serialization_limits{};
   deadline_limits.max_serialization_time = std::chrono::microseconds{0};
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::abi_json_to_bin(empty_abi(), "uint8", forge::variant{1}, deadline_limits)),
       chain_api::abi_serialization_error,
       [](const auto& error) { return error.diagnostic().code == chain_api::abi_error_code::deadline_exceeded; });

   auto binary_limits = chain_api::abi_serialization_limits{};
   binary_limits.max_binary_bytes = 2;
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::abi_json_to_bin(empty_abi(), "string", forge::variant{"abc"}, binary_limits)),
       chain_api::abi_serialization_error,
       [](const auto& error) { return error.diagnostic().code == chain_api::abi_error_code::size_limit; });

   auto array_limits = chain_api::abi_serialization_limits{};
   array_limits.max_container_elements = 2;
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::abi_json_to_bin(empty_abi(), "uint8[]", array(1, 2, 3), array_limits)),
       chain_api::abi_serialization_error,
       [](const auto& error) { return error.diagnostic().code == chain_api::abi_error_code::size_limit; });

   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::abi_bin_to_json(
                             empty_abi(), "uint8[]", protocol::bytes{0xff, 0xff, 0xff, 0xff, 0x0f}, array_limits)),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                                   error.diagnostic().offset == 5U;
                         });
}

BOOST_AUTO_TEST_CASE(chain_abi_rejects_oversized_json_bytes_before_hex_decode) {
   auto limits = chain_api::abi_serialization_limits{};
   limits.max_string_bytes = 1U;

   BOOST_TEST(chain_api::abi_json_to_bin(empty_abi(), "bytes", forge::variant{"ff"}, limits) ==
              protocol::bytes({0x01, 0xff}));
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::abi_json_to_bin(empty_abi(), "bytes", forge::variant{"zzzz"}, limits)),
       chain_api::abi_serialization_error, [](const auto& error) {
          return error.diagnostic().code == chain_api::abi_error_code::size_limit &&
                 error.diagnostic().type == "bytes" && error.diagnostic().path == "bytes" &&
                 error.diagnostic().offset == 0U;
       });
}

BOOST_AUTO_TEST_CASE(chain_abi_rejects_invalid_definition_shapes) {
   auto extension_abi = empty_abi();
   extension_abi.structs = {
       protocol::struct_def{
           .name = "bad",
           .fields =
               {
                   protocol::field_def{.name = "extension", .type = "uint8$"},
                   protocol::field_def{.name = "required", .type = "uint8"},
               },
       },
   };
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(chain_api::abi_json_to_bin(extension_abi, "bad", object({{"extension", 1}, {"required", 2}}))),
       chain_api::abi_serialization_error, [](const auto& error) {
          return error.diagnostic().code == chain_api::abi_error_code::invalid_abi &&
                 error.diagnostic().path == "bad.required";
       });

   auto circular_abi = empty_abi();
   circular_abi.types = {
       protocol::type_def{.new_type_name = "a", .type = "b"},
       protocol::type_def{.new_type_name = "b", .type = "a"},
   };
   BOOST_CHECK_EXCEPTION(static_cast<void>(chain_api::abi_json_to_bin(circular_abi, "a", forge::variant{1})),
                         chain_api::abi_serialization_error, [](const auto& error) {
                            return error.diagnostic().code == chain_api::abi_error_code::circular_definition;
                         });
}
