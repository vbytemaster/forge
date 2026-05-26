module;
#include <fcl/exception/macros.hpp>

#include <cctype>
#include <cstring>
#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module fcl.crypto.webauthn;

import fcl.crypto.base64;
import fcl.crypto.p256;
import fcl.crypto.openssl;
import fcl.crypto.sha256;
import fcl.exception.exception;

namespace fcl::crypto::webauthn {

namespace detail {
using namespace std::literals;

struct client_data_fields {
   std::string challenge;
   std::string origin;
   std::string type;
};

class client_data_json_reader {
 public:
   explicit client_data_json_reader(std::string_view input) : _input(input) {}

   client_data_fields parse() {
      auto fields = client_data_fields{};
      skip_ws();
      parse_top_object(fields);
      skip_ws();
      if (_pos != _input.size()) {
         fail("trailing characters after top-level object");
      }
      return fields;
   }

 private:
   static constexpr std::size_t max_json_depth = 64;

   void parse_top_object(client_data_fields& fields) {
      expect('{');
      skip_ws();
      if (consume('}')) {
         return;
      }

      while (true) {
         skip_ws();
         const auto key = parse_string();
         skip_ws();
         expect(':');
         skip_ws();

         if (key == "challenge") {
            fields.challenge = parse_required_string("challenge");
         } else if (key == "origin") {
            fields.origin = parse_required_string("origin");
         } else if (key == "type") {
            fields.type = parse_required_string("type");
         } else {
            skip_value(1);
         }

         skip_ws();
         if (consume('}')) {
            return;
         }
         expect(',');
      }
   }

   std::string parse_required_string(std::string_view field) {
      if (!peek('"')) {
         fail("expected string value for field " + std::string(field));
      }
      return parse_string();
   }

   void skip_value(std::size_t depth) {
      ensure_depth(depth);
      skip_ws();
      if (peek('"')) {
         (void)parse_string();
      } else if (consume('{')) {
         skip_object_body(depth + 1);
      } else if (consume('[')) {
         skip_array_body(depth + 1);
      } else if (peek('-') || peek_digit()) {
         skip_number();
      } else if (consume_literal("true") || consume_literal("false") || consume_literal("null")) {
         return;
      } else {
         fail("expected JSON value");
      }
   }

   void skip_object_body(std::size_t depth) {
      ensure_depth(depth);
      skip_ws();
      if (consume('}')) {
         return;
      }
      while (true) {
         skip_ws();
         (void)parse_string();
         skip_ws();
         expect(':');
         skip_value(depth);
         skip_ws();
         if (consume('}')) {
            return;
         }
         expect(',');
      }
   }

   void skip_array_body(std::size_t depth) {
      ensure_depth(depth);
      skip_ws();
      if (consume(']')) {
         return;
      }
      while (true) {
         skip_value(depth);
         skip_ws();
         if (consume(']')) {
            return;
         }
         expect(',');
      }
   }

   void skip_number() {
      if (consume('-') && !peek_digit()) {
         fail("invalid JSON number");
      }
      if (consume('0')) {
         if (peek_digit()) {
            fail("invalid JSON number");
         }
      } else {
         require_digit();
         while (peek_digit()) {
            ++_pos;
         }
      }
      if (consume('.')) {
         require_digit();
         while (peek_digit()) {
            ++_pos;
         }
      }
      if (consume('e') || consume('E')) {
         (void)(consume('+') || consume('-'));
         require_digit();
         while (peek_digit()) {
            ++_pos;
         }
      }
   }

   std::string parse_string() {
      expect('"');
      auto out = std::string{};
      while (_pos < _input.size()) {
         const auto ch = _input[_pos++];
         if (ch == '"') {
            return out;
         }
         if (static_cast<unsigned char>(ch) < 0x20) {
            fail("unescaped control character in JSON string");
         }
         if (ch != '\\') {
            out.push_back(ch);
            continue;
         }
         if (_pos >= _input.size()) {
            fail("unterminated escape sequence");
         }
         const auto escaped = _input[_pos++];
         switch (escaped) {
         case '"':
            out.push_back('"');
            break;
         case '\\':
            out.push_back('\\');
            break;
         case '/':
            out.push_back('/');
            break;
         case 'b':
            out.push_back('\b');
            break;
         case 'f':
            out.push_back('\f');
            break;
         case 'n':
            out.push_back('\n');
            break;
         case 'r':
            out.push_back('\r');
            break;
         case 't':
            out.push_back('\t');
            break;
         case 'u':
            append_unicode_escape(out);
            break;
         default:
            fail("invalid JSON escape");
         }
      }
      fail("unterminated JSON string");
   }

   void append_unicode_escape(std::string& out) {
      auto codepoint = parse_hex_quad();
      if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
         if (!consume('\\') || !consume('u')) {
            fail("missing low surrogate in JSON string");
         }
         const auto low = parse_hex_quad();
         if (low < 0xDC00 || low > 0xDFFF) {
            fail("invalid low surrogate in JSON string");
         }
         codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
      } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
         fail("unexpected low surrogate in JSON string");
      }
      append_utf8(out, codepoint);
   }

   std::uint32_t parse_hex_quad() {
      auto value = std::uint32_t{0};
      for (auto i = 0; i < 4; ++i) {
         if (_pos >= _input.size()) {
            fail("truncated unicode escape");
         }
         value = (value << 4) | hex_value(_input[_pos++]);
      }
      return value;
   }

   static void append_utf8(std::string& out, std::uint32_t codepoint) {
      if (codepoint <= 0x7F) {
         out.push_back(static_cast<char>(codepoint));
      } else if (codepoint <= 0x7FF) {
         out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
         out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
      } else if (codepoint <= 0xFFFF) {
         out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
         out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
         out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
      } else {
         out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
         out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
         out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
         out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
      }
   }

   static std::uint32_t hex_value(char ch) {
      if (ch >= '0' && ch <= '9') {
         return static_cast<std::uint32_t>(ch - '0');
      }
      if (ch >= 'a' && ch <= 'f') {
         return static_cast<std::uint32_t>(ch - 'a' + 10);
      }
      if (ch >= 'A' && ch <= 'F') {
         return static_cast<std::uint32_t>(ch - 'A' + 10);
      }
      throw std::invalid_argument{"invalid hex digit"};
   }

   bool consume(char expected) {
      if (peek(expected)) {
         ++_pos;
         return true;
      }
      return false;
   }

   bool consume_literal(std::string_view literal) {
      if (_input.substr(_pos, literal.size()) == literal) {
         _pos += literal.size();
         return true;
      }
      return false;
   }

   void expect(char expected) {
      if (!consume(expected)) {
         auto message = std::string{"expected '"};
         message.push_back(expected);
         message.push_back('\'');
         fail(message);
      }
   }

   bool peek(char expected) const {
      return _pos < _input.size() && _input[_pos] == expected;
   }

   bool peek_digit() const {
      return _pos < _input.size() && std::isdigit(static_cast<unsigned char>(_input[_pos])) != 0;
   }

   void require_digit() {
      if (!peek_digit()) {
         fail("expected digit");
      }
      ++_pos;
   }

   void skip_ws() {
      while (_pos < _input.size() && std::isspace(static_cast<unsigned char>(_input[_pos])) != 0) {
         ++_pos;
      }
   }

   void ensure_depth(std::size_t depth) const {
      if (depth > max_json_depth) {
         fail("JSON nesting depth exceeded");
      }
   }

   [[noreturn]] void fail(const std::string& reason) const {
      FCL_THROW("Failed to parse client data JSON", fcl::exception::ctx("reason", reason), fcl::exception::ctx("offset", _pos));
   }

   std::string_view _input;
   std::size_t _pos = 0;
};

client_data_fields parse_client_data_json(std::string_view input) {
   try {
      return client_data_json_reader{input}.parse();
   } catch (const std::invalid_argument& e) {
      FCL_THROW("Failed to parse client data JSON", fcl::exception::ctx("reason", e.what()));
   }
}

} // namespace detail

public_key::public_key(const signature& c, const fcl::crypto::sha256& digest, bool) {
   const auto client_data = detail::parse_client_data_json(c.client_json);

   FCL_ASSERT(client_data.type == "webauthn.get", "webauthn signature type not an assertion");

   std::vector<char> challenge_bytes = fcl::crypto::base64url_decode(client_data.challenge);
   FCL_ASSERT(fcl::crypto::sha256(challenge_bytes.data(), challenge_bytes.size()) == digest, "Wrong webauthn challenge");

   char required_origin_scheme[] = "https://";
   size_t https_len = strlen(required_origin_scheme);
   FCL_ASSERT(client_data.origin.compare(0, https_len, required_origin_scheme) == 0,
              "webauthn origin must begin with https://");
   rpid = client_data.origin.substr(https_len, client_data.origin.rfind(':') - https_len);

   constexpr static size_t min_auth_data_size = 37;
   FCL_ASSERT(c.auth_data.size() >= min_auth_data_size, "auth_data not as large as required");
   if (c.auth_data[32] & 0x01)
      user_verification_type = user_presence_t::USER_PRESENCE_PRESENT;
   if (c.auth_data[32] & 0x04)
      user_verification_type = user_presence_t::USER_PRESENCE_VERIFIED;

   static_assert(min_auth_data_size >= sizeof(fcl::crypto::sha256), "auth_data min size not enough to store a sha256");
   FCL_ASSERT(memcmp(c.auth_data.data(), fcl::crypto::sha256::hash(rpid).data(), sizeof(fcl::crypto::sha256)) == 0,
              "webauthn rpid hash doesn't match origin");

   // the signature (and thus public key we need to return) will be over
   //  sha256(auth_data || client_data_hash)
   fcl::crypto::sha256 client_data_hash = fcl::crypto::sha256::hash(c.client_json);
   fcl::crypto::sha256::encoder e;
   e.write((char*)c.auth_data.data(), c.auth_data.size());
   e.write(client_data_hash.data(), client_data_hash.data_size());
   fcl::crypto::sha256 signed_digest = e.result();

   int nV = c.compact_signature.data()[0];
   if (nV < 31 || nV >= 35)
      FCL_THROW("unable to reconstruct public key from signature");
   public_key_data = p256::recover_public_key_data(c.compact_signature, signed_digest, false);
}

void public_key::post_init() {
   FCL_ASSERT(rpid.length(), "webauthn pubkey must have non empty rpid");
}

} // namespace fcl::crypto::webauthn
