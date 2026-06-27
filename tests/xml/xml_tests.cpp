#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge_xml_tests {

struct object_entry {
   std::string key;
   std::uint64_t size = 0;
};

struct list_bucket_result {
   std::string name;
   std::vector<object_entry> contents;
   std::optional<std::string> next_continuation_token;
};

struct delete_result {
   std::vector<object_entry> deleted;
};

struct complete_multipart_upload {
   std::string location;
   std::string bucket;
   std::string key;
   std::string etag;
};

struct error_body {
   std::string code;
   std::string message;
   std::string request_id;
};

struct ordered_child {
   std::string b;
   std::string a;
};

struct ordered_parent {
   std::vector<ordered_child> children;
};

struct schema_bound_child {
   std::string a;
   std::string omitted;
   std::string b;
};

struct schema_bound_parent {
   std::vector<schema_bound_child> children;
};

} // namespace forge_xml_tests

namespace forge_xml_tests {

BOOST_DESCRIBE_STRUCT(object_entry, (), (key, size))
BOOST_DESCRIBE_STRUCT(list_bucket_result, (), (name, contents, next_continuation_token))
BOOST_DESCRIBE_STRUCT(delete_result, (), (deleted))
BOOST_DESCRIBE_STRUCT(complete_multipart_upload, (), (location, bucket, key, etag))
BOOST_DESCRIBE_STRUCT(error_body, (), (code, message, request_id))
BOOST_DESCRIBE_STRUCT(ordered_child, (), (b, a))
BOOST_DESCRIBE_STRUCT(ordered_parent, (), (children))
BOOST_DESCRIBE_STRUCT(schema_bound_child, (), (a, omitted, b))
BOOST_DESCRIBE_STRUCT(schema_bound_parent, (), (children))

} // namespace forge_xml_tests

import forge.schema.object;
import forge.schema.diagnostic;
import forge.xml;

template <> struct forge::schema::rules<forge_xml_tests::object_entry> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::object_entry> define() {
      auto schema = forge::schema::object<forge_xml_tests::object_entry>();
      schema.field<&forge_xml_tests::object_entry::key>("Key").required().non_empty();
      schema.field<&forge_xml_tests::object_entry::size>("Size").required();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::list_bucket_result> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::list_bucket_result> define() {
      auto schema = forge::schema::object<forge_xml_tests::list_bucket_result>();
      schema.field<&forge_xml_tests::list_bucket_result::name>("Name").required().non_empty();
      schema.field<&forge_xml_tests::list_bucket_result::contents>("Contents")
          .items<forge_xml_tests::object_entry>();
      schema.field<&forge_xml_tests::list_bucket_result::next_continuation_token>("NextContinuationToken");
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::delete_result> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::delete_result> define() {
      auto schema = forge::schema::object<forge_xml_tests::delete_result>();
      schema.field<&forge_xml_tests::delete_result::deleted>("Deleted").items<forge_xml_tests::object_entry>();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::complete_multipart_upload> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::complete_multipart_upload> define() {
      auto schema = forge::schema::object<forge_xml_tests::complete_multipart_upload>();
      schema.field<&forge_xml_tests::complete_multipart_upload::location>("Location").required().non_empty();
      schema.field<&forge_xml_tests::complete_multipart_upload::bucket>("Bucket").required().non_empty();
      schema.field<&forge_xml_tests::complete_multipart_upload::key>("Key").required().non_empty();
      schema.field<&forge_xml_tests::complete_multipart_upload::etag>("ETag").required().non_empty();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::error_body> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::error_body> define() {
      auto schema = forge::schema::object<forge_xml_tests::error_body>();
      schema.field<&forge_xml_tests::error_body::code>("Code").required().non_empty();
      schema.field<&forge_xml_tests::error_body::message>("Message").required().non_empty();
      schema.field<&forge_xml_tests::error_body::request_id>("RequestId").required().non_empty();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::ordered_child> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::ordered_child> define() {
      auto schema = forge::schema::object<forge_xml_tests::ordered_child>();
      schema.field<&forge_xml_tests::ordered_child::b>("B").required().non_empty();
      schema.field<&forge_xml_tests::ordered_child::a>("A").required().non_empty();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::ordered_parent> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::ordered_parent> define() {
      auto schema = forge::schema::object<forge_xml_tests::ordered_parent>();
      schema.field<&forge_xml_tests::ordered_parent::children>("Child").items<forge_xml_tests::ordered_child>();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::schema_bound_child> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::schema_bound_child> define() {
      auto schema = forge::schema::object<forge_xml_tests::schema_bound_child>();
      schema.field<&forge_xml_tests::schema_bound_child::b>("B").required().non_empty();
      schema.field<&forge_xml_tests::schema_bound_child::a>("A").required().non_empty();
      return schema;
   }
};

template <> struct forge::schema::rules<forge_xml_tests::schema_bound_parent> {
   [[nodiscard]] static forge::schema::object_schema<forge_xml_tests::schema_bound_parent> define() {
      auto schema = forge::schema::object<forge_xml_tests::schema_bound_parent>();
      schema.field<&forge_xml_tests::schema_bound_parent::children>("Child").items<forge_xml_tests::schema_bound_child>();
      return schema;
   }
};

namespace {

[[nodiscard]] bool has_error_code(const std::vector<forge::schema::diagnostic>& diagnostics,
                                  std::string_view code) {
   return std::ranges::any_of(diagnostics, [&](const forge::schema::diagnostic& entry) {
      return entry.level == forge::schema::severity::error && entry.code == code;
   });
}

} // namespace

BOOST_AUTO_TEST_SUITE(xml_codec_tests)

BOOST_AUTO_TEST_CASE(xml_tree_parse_write_roundtrip_preserves_generic_shape) {
   const auto parsed = forge::xml::read_value(
       R"(<?xml version="1.0"?><Bucket xmlns="urn:s3"><Item id="a">alpha</Item><Item id="b">beta</Item></Bucket>)");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.root.name == "Bucket");
   BOOST_REQUIRE_EQUAL(parsed.value.root.attributes.size(), 1U);
   BOOST_TEST(parsed.value.root.attributes.front().name == "xmlns");
   BOOST_TEST(parsed.value.root.attributes.front().value == "urn:s3");
   BOOST_REQUIRE_EQUAL(parsed.value.root.children.size(), 2U);
   BOOST_TEST(parsed.value.root.children[0].name == "Item");
   BOOST_TEST(parsed.value.root.children[0].attributes.front().name == "id");
   BOOST_TEST(parsed.value.root.children[0].text == "alpha");

   const auto written = forge::xml::write_value(parsed.value, {.pretty = true});
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("<Bucket") != std::string::npos);
   BOOST_TEST(written.text.find("xmlns=\"urn:s3\"") != std::string::npos);

   const auto reparsed = forge::xml::read_value(written.text);
   BOOST_REQUIRE(reparsed.ok());
   BOOST_TEST(reparsed.value.root.children[1].text == "beta");
}

BOOST_AUTO_TEST_CASE(xml_write_value_can_add_root_default_namespace) {
   auto doc = forge::xml::document{.root = forge::xml::element{.name = "ListBucketResult"}};
   doc.root.children.push_back(forge::xml::element{.name = "Name", .text = "photos"});

   const auto written = forge::xml::write_value(doc, {.default_namespace = "urn:aws:s3"});
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("xmlns=\"urn:aws:s3\"") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(xml_typed_s3_list_bucket_uses_schema_names_and_repeated_children) {
   const auto parsed = forge::xml::read<forge_xml_tests::list_bucket_result>(
       R"(<ListBucketResult><Name>photos</Name><Contents><Key>a.jpg</Key><Size>12</Size></Contents><Contents><Key>b.jpg</Key><Size>34</Size></Contents></ListBucketResult>)");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.name == "photos");
   BOOST_REQUIRE_EQUAL(parsed.value.contents.size(), 2U);
   BOOST_TEST(parsed.value.contents[0].key == "a.jpg");
   BOOST_TEST(parsed.value.contents[1].size == 34U);
   BOOST_TEST(!parsed.value.next_continuation_token.has_value());

   const auto written = forge::xml::write(parsed.value, {.root_name = "ListBucketResult", .pretty = true});
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("<Name>photos</Name>") != std::string::npos);
   BOOST_TEST(written.text.find("<Contents>") != std::string::npos);
   BOOST_TEST(written.text.find("<Key>a.jpg</Key>") != std::string::npos);
   BOOST_TEST(written.text.find("next_continuation_token") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(xml_schema_read_rejects_duplicate_scalar_fields) {
   const auto parsed = forge::xml::read<forge_xml_tests::list_bucket_result>(
       R"(<ListBucketResult><Name>photos</Name><Name>archive</Name></ListBucketResult>)");

   BOOST_TEST(!parsed.ok());
   BOOST_TEST(has_error_code(parsed.diagnostics, "xml.duplicate"));
}

BOOST_AUTO_TEST_CASE(xml_schema_write_preserves_nested_object_field_order) {
   const auto value = forge_xml_tests::ordered_parent{.children = {{.b = "first", .a = "second"}}};
   const auto written = forge::xml::write(value, {.root_name = "Root"});

   BOOST_REQUIRE(written.ok());
   const auto b = written.text.find("<B>first</B>");
   const auto a = written.text.find("<A>second</A>");
   BOOST_REQUIRE(b != std::string::npos);
   BOOST_REQUIRE(a != std::string::npos);
   BOOST_TEST(b < a);
}

BOOST_AUTO_TEST_CASE(xml_schema_write_binds_schema_fields_to_declared_members) {
   const auto value =
      forge_xml_tests::schema_bound_parent{.children = {{.a = "alpha", .omitted = "hidden", .b = "bravo"}}};
   const auto written = forge::xml::write(value, {.root_name = "Root"});

   BOOST_REQUIRE(written.ok());
   const auto b = written.text.find("<B>bravo</B>");
   const auto a = written.text.find("<A>alpha</A>");
   BOOST_REQUIRE(b != std::string::npos);
   BOOST_REQUIRE(a != std::string::npos);
   BOOST_TEST(b < a);
   BOOST_TEST(written.text.find("hidden") == std::string::npos);
   BOOST_TEST(written.text.find("omitted") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(xml_typed_unknown_field_policy_matches_forge_diagnostics) {
   const auto warned = forge::xml::read<forge_xml_tests::list_bucket_result>(
       R"(<ListBucketResult><Name>photos</Name><Unexpected>1</Unexpected></ListBucketResult>)");
   BOOST_REQUIRE(warned.ok());
   BOOST_REQUIRE_EQUAL(warned.diagnostics.size(), 1U);
   BOOST_TEST(warned.diagnostics.front().code == "xml.unknown");

   auto options = forge::xml::read_options{};
   options.unknown_fields = forge::xml::unknown_field_policy::error;
   const auto rejected = forge::xml::read<forge_xml_tests::list_bucket_result>(
       R"(<ListBucketResult><Name>photos</Name><Unexpected>1</Unexpected></ListBucketResult>)", options);
   BOOST_TEST(!rejected.ok());
   BOOST_TEST(has_error_code(rejected.diagnostics, "xml.unknown"));
}

BOOST_AUTO_TEST_CASE(xml_s3_shaped_delete_complete_multipart_and_error_bodies_roundtrip) {
   const auto deleted =
       forge::xml::read<forge_xml_tests::delete_result>(R"(<DeleteResult><Deleted><Key>old.txt</Key><Size>0</Size></Deleted></DeleteResult>)");
   BOOST_REQUIRE(deleted.ok());
   BOOST_REQUIRE_EQUAL(deleted.value.deleted.size(), 1U);
   BOOST_TEST(deleted.value.deleted.front().key == "old.txt");

   const auto completed = forge::xml::read<forge_xml_tests::complete_multipart_upload>(
       R"(<CompleteMultipartUploadResult><Location>/bucket/key</Location><Bucket>bucket</Bucket><Key>key</Key><ETag>"abc"</ETag></CompleteMultipartUploadResult>)");
   BOOST_REQUIRE(completed.ok());
   BOOST_TEST(completed.value.bucket == "bucket");
   BOOST_TEST(completed.value.etag == "\"abc\"");

   const auto error = forge::xml::read<forge_xml_tests::error_body>(
       R"(<Error><Code>NoSuchKey</Code><Message>missing</Message><RequestId>req-1</RequestId></Error>)");
   BOOST_REQUIRE(error.ok());
   BOOST_TEST(error.value.code == "NoSuchKey");
}

BOOST_AUTO_TEST_CASE(xml_malformed_and_unsafe_inputs_return_forge_diagnostics_without_backend_names) {
   const auto malformed = forge::xml::read_value("<Root>");
   BOOST_TEST(!malformed.ok());
   BOOST_TEST(has_error_code(malformed.diagnostics, "xml.parse"));
   BOOST_TEST(malformed.diagnostics.front().message.find("pugi") == std::string::npos);

   const auto dtd = forge::xml::read_value("<!DOCTYPE Root [ <!ENTITY x SYSTEM \"file:///tmp/x\"> ]><Root>&x;</Root>");
   BOOST_TEST(!dtd.ok());
   BOOST_TEST(has_error_code(dtd.diagnostics, "xml.unsafe"));

   const auto processing_instruction = forge::xml::read_value("<?xml version=\"1.0\"?><?work test?><Root/>");
   BOOST_TEST(!processing_instruction.ok());
   BOOST_TEST(has_error_code(processing_instruction.diagnostics, "xml.unsafe"));

   const auto comment = forge::xml::read_value("<Root><!-- hidden --></Root>");
   BOOST_TEST(!comment.ok());
   BOOST_TEST(has_error_code(comment.diagnostics, "xml.unsafe"));
}

BOOST_AUTO_TEST_CASE(xml_limits_are_enforced_for_input_tree_and_output) {
   auto options = forge::xml::read_options{.max_bytes = 4};
   BOOST_TEST(!forge::xml::read_value("<Root/>", options).ok());

   options = forge::xml::read_options{.max_depth = 1};
   BOOST_TEST(has_error_code(forge::xml::read_value("<A><B><C/></B></A>", options).diagnostics, "xml.depth"));

   options = forge::xml::read_options{.max_attributes = 1};
   BOOST_TEST(has_error_code(forge::xml::read_value("<A a=\"1\" b=\"2\"/>", options).diagnostics,
                             "xml.attributes"));

   options = forge::xml::read_options{.max_children = 1};
   BOOST_TEST(has_error_code(forge::xml::read_value("<A><B/><C/></A>", options).diagnostics, "xml.children"));

   options = forge::xml::read_options{.max_text_bytes = 3};
   BOOST_TEST(has_error_code(forge::xml::read_value("<A>abcd</A>", options).diagnostics, "xml.text"));

   auto doc = forge::xml::document{.root = forge::xml::element{.name = "Root", .text = "too-long"}};
   const auto written = forge::xml::write_value(doc, {.max_bytes = 4});
   BOOST_TEST(!written.ok());
   BOOST_TEST(has_error_code(written.diagnostics, "xml.max-bytes"));
}

BOOST_AUTO_TEST_SUITE_END()
