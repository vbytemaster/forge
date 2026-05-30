#include <boost/test/unit_test.hpp>
#include <fcl/exception/macros.hpp>

#include <chrono>
#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <system_error>

import fcl.exception.exception;

namespace test_http_exceptions {

enum class code : int {
   not_found = 404,
   internal = 500,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "test.http")

using not_found = fcl::exception::coded_exception<code, code::not_found>;

} // namespace test_http_exceptions

namespace test_product_exceptions {

enum class code : std::uint8_t {
   chunk_not_found = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "test.cache")

using chunk_not_found = fcl::exception::coded_exception<code, code::chunk_not_found>;

} // namespace test_product_exceptions

BOOST_AUTO_TEST_SUITE(exception_test_suite)

BOOST_AUTO_TEST_CASE(context_fields_are_formatted_and_redacted) {
   const fcl::exception::context_error error{
       "open vault",
       {fcl::exception::ctx("path", "/tmp/fcl.vault"), fcl::exception::secret("passphrase", "correct horse battery staple")},
       std::source_location::current()};

   const std::string text = error.what();
   BOOST_CHECK(text.find("open vault") != std::string::npos);
   BOOST_CHECK(text.find("path=/tmp/fcl.vault") != std::string::npos);
   BOOST_CHECK(text.find("passphrase=<redacted>") != std::string::npos);
   BOOST_CHECK(text.find("correct horse battery staple") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(context_error_keeps_optional_error_code) {
   const fcl::exception::context_error error{"deadline exceeded",
                                         {fcl::exception::ctx("phase", "startup")},
                                         std::source_location::current(),
                                         std::make_error_code(std::errc::timed_out)};

   BOOST_CHECK_EQUAL(error.message(), "deadline exceeded");
   BOOST_CHECK_EQUAL(error.context().size(), 1u);
   BOOST_CHECK(error.code() == std::make_error_code(std::errc::timed_out));
   BOOST_CHECK(std::string(error.what()).find("timed out") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(throw_exception_throws_concrete_coded_exception) {
   try {
      FCL_THROW_EXCEPTION(test_http_exceptions::not_found, "route not found", fcl::exception::ctx("path", "/missing"));
   } catch (const test_http_exceptions::not_found& error) {
      BOOST_CHECK_EQUAL(error.message(), "route not found");
      BOOST_CHECK_EQUAL(error.code().value(), 404);
      BOOST_CHECK_EQUAL(std::string(error.code().category().name()), "test.http");
      BOOST_CHECK(std::string(error.what()).find("path=/missing") != std::string::npos);
      return;
   }

   BOOST_FAIL("expected concrete typed exception");
}

BOOST_AUTO_TEST_CASE(custom_uint8_exception_category_works) {
   try {
      FCL_THROW_EXCEPTION(test_product_exceptions::chunk_not_found, "chunk not found");
   } catch (const test_product_exceptions::chunk_not_found& error) {
      BOOST_CHECK_EQUAL(error.code().value(), 1);
      BOOST_CHECK_EQUAL(std::string(error.code().category().name()), "test.cache");
      return;
   }

   BOOST_FAIL("expected product typed exception");
}

BOOST_AUTO_TEST_CASE(throw_code_throws_runtime_coded_exception_with_call_site_context) {
   const auto runtime_code = test_http_exceptions::code::internal;
   unsigned expected_line = 0;

   try {
      expected_line = __LINE__ + 1;
      FCL_THROW_CODE(runtime_code, "dynamic failure", fcl::exception::ctx("phase", "runtime-map"));
   } catch (const fcl::exception::runtime_coded_exception<test_http_exceptions::code>& error) {
      BOOST_CHECK_EQUAL(static_cast<int>(error.value()), static_cast<int>(test_http_exceptions::code::internal));
      BOOST_CHECK_EQUAL(error.message(), "dynamic failure");
      BOOST_CHECK_EQUAL(error.code().value(), 500);
      BOOST_CHECK_EQUAL(std::string(error.code().category().name()), "test.http");
      BOOST_CHECK_EQUAL(error.context().size(), 1u);
      BOOST_CHECK_EQUAL(error.context().front().key, "phase");
      BOOST_CHECK_EQUAL(error.context().front().value, "runtime-map");
      BOOST_CHECK_EQUAL(error.location().line(), expected_line);
      BOOST_CHECK(std::string(error.location().file_name()).find("exception_tests.cpp") != std::string::npos);
      return;
   }

   BOOST_FAIL("expected runtime coded exception");
}

BOOST_AUTO_TEST_CASE(capture_and_rethrow_preserves_nested_exception) {
   try {
      try {
         throw std::runtime_error("inner failure");
      }
      FCL_CAPTURE_AND_RETHROW("outer context", fcl::exception::ctx("phase", "initialize"))
   } catch (const fcl::exception::context_error& error) {
      const auto chain = fcl::exception::format_exception_chain(error);
      BOOST_CHECK(chain.find("outer context") != std::string::npos);
      BOOST_CHECK(chain.find("phase=initialize") != std::string::npos);
      BOOST_CHECK(chain.find("inner failure") != std::string::npos);
      return;
   }

   BOOST_FAIL("expected context_error");
}

BOOST_AUTO_TEST_CASE(capture_and_rethrow_preserves_fcl_exception_dynamic_type) {
   try {
      try {
         FCL_THROW_EXCEPTION(test_product_exceptions::chunk_not_found, "chunk not found",
                             fcl::exception::ctx("ref", "bafk..."));
      }
      FCL_CAPTURE_AND_RETHROW("read cache", fcl::exception::ctx("phase", "lookup"))
   } catch (const test_product_exceptions::chunk_not_found& error) {
      const std::string text = error.what();
      BOOST_CHECK_EQUAL(error.code().value(), 1);
      BOOST_CHECK_EQUAL(error.context_frames().size(), 1u);
      BOOST_CHECK(text.find("chunk not found") != std::string::npos);
      BOOST_CHECK(text.find("read cache") != std::string::npos);
      BOOST_CHECK(text.find("phase=lookup") != std::string::npos);
      return;
   }

   BOOST_FAIL("expected original typed exception to be preserved");
}

BOOST_AUTO_TEST_CASE(assert_macro_throws_std_compatible_context_error) {
   BOOST_CHECK_EXCEPTION(FCL_ASSERT(false, "broken invariant", fcl::exception::ctx("value", 42)), fcl::exception::context_error,
                         [](const fcl::exception::context_error& error) {
                            const std::string text = error.what();
                            return error.code() == std::make_error_code(std::errc::invalid_argument) &&
                                   text.find("broken invariant") != std::string::npos &&
                                   text.find("expression=false") != std::string::npos &&
                                   text.find("value=42") != std::string::npos;
                         });
}

BOOST_AUTO_TEST_CASE(deadline_macro_throws_timeout_context_error) {
   BOOST_CHECK_EXCEPTION(FCL_CHECK_DEADLINE(std::chrono::system_clock::now() - std::chrono::milliseconds(1),
                                            fcl::exception::ctx("phase", "render")),
                         fcl::exception::context_error, [](const fcl::exception::context_error& error) {
                            return error.code() == std::make_error_code(std::errc::timed_out) &&
                                   std::string(error.what()).find("phase=render") != std::string::npos;
                         });
}

BOOST_AUTO_TEST_SUITE_END()
