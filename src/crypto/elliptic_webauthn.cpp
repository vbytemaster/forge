#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/openssl.hpp>

#include <fc/fwd_impl.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>

#define RAPIDJSON_NAMESPACE_BEGIN namespace fc::crypto::webauthn::detail::rapidjson {
#define RAPIDJSON_NAMESPACE_END }
#include "rapidjson/reader.h"

#include <string>

namespace fc { namespace crypto { namespace webauthn {

namespace detail {
using namespace std::literals;

struct webauthn_json_handler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, webauthn_json_handler> {
   std::string found_challenge;
   std::string found_origin;
   std::string found_type;

   enum parse_stat_t {
      EXPECT_FIRST_OBJECT_START,
      EXPECT_FIRST_OBJECT_KEY,
      EXPECT_FIRST_OBJECT_DONTCARE_VALUE,
      EXPECT_CHALLENGE_VALUE,
      EXPECT_ORIGIN_VALUE,
      EXPECT_TYPE_VALUE,
      IN_NESTED_CONTAINER
   } current_state = EXPECT_FIRST_OBJECT_START;
   unsigned current_nested_container_depth = 0;

   bool basic_stuff() {
      if(current_state == IN_NESTED_CONTAINER)
         return true;
      if(current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE) {
         current_state = EXPECT_FIRST_OBJECT_KEY;
         return true;
      }
      return false;
   }

   bool Null() {
      return basic_stuff();
   }
   bool Bool(bool) {
      return basic_stuff();
   }
   bool Int(int) {
      return basic_stuff();
   }
   bool Uint(unsigned) {
      return basic_stuff();
   }
   bool Int64(int64_t) {
      return basic_stuff();
   }
   bool Uint64(uint64_t) {
      return basic_stuff();
   }
   bool Double(double) {
      return basic_stuff();
   }

   bool String(const char* str, rapidjson::SizeType length, bool copy) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_KEY:
            return false;
         case EXPECT_CHALLENGE_VALUE:
            found_challenge = std::string(str, length);
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_ORIGIN_VALUE:
            found_origin = std::string(str, length);
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_TYPE_VALUE:
            found_type = std::string(str, length);
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case IN_NESTED_CONTAINER:
            return true;
         // default may not be added now, as consensus could be broken.
      }
      // Add elog to report the error and __builtin_unreachable to surpress
      // warning of control reaches end of non-void function.
      elog("String: current_state(${current_statea}) out of bound", ("current_state", current_state) );
      __builtin_unreachable();
   }

   bool StartObject() {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
            current_state = IN_NESTED_CONTAINER;
            ++current_nested_container_depth;
            return true;
         case IN_NESTED_CONTAINER:
            ++current_nested_container_depth;
            return true;
         case EXPECT_FIRST_OBJECT_KEY:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
         case EXPECT_TYPE_VALUE:
            return false;
         // default may not be added now, as consensus could be broken.
      }
      // Add elog to report the error and __builtin_unreachable to surpress
      // warning of control reaches end of non-void function.
      elog( "Key: current_state (${current_statea}) out of bound", ("current_state", current_state) );
      __builtin_unreachable();
   }
   bool Key(const char* str, rapidjson::SizeType length, bool copy) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
         case EXPECT_TYPE_VALUE:
            return false;
         case EXPECT_FIRST_OBJECT_KEY: {
            if("challenge"s == str)
               current_state = EXPECT_CHALLENGE_VALUE;
            else if("origin"s == str)
               current_state = EXPECT_ORIGIN_VALUE;
            else if("type"s == str)
               current_state = EXPECT_TYPE_VALUE;
            else
               current_state = EXPECT_FIRST_OBJECT_DONTCARE_VALUE;
            return true;
         }
         case IN_NESTED_CONTAINER:
            return true;
         // default may not be added now, as consensus could be broken.
      }
      // Add elog to report the error and __builtin_unreachable to surpress
      // warning of control reaches end of non-void function.
      elog( "Key: current_state (${current_statea}) out of bound", ("current_state", current_state) );
      __builtin_unreachable();
   }
   bool EndObject(rapidjson::SizeType memberCount) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
         case EXPECT_TYPE_VALUE:
            return false;
         case IN_NESTED_CONTAINER:
            if(!--current_nested_container_depth)
               current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_FIRST_OBJECT_KEY:
            return true;
         // default may not be added now, as consensus could be broken.
      }
      // Add elog to report the error and __builtin_unreachable to surpress
      // warning of control reaches end of non-void function.
      elog( "EndObject: current_state (${current_statea}) out of bound", ("current_state", current_state) );
      __builtin_unreachable();
   }

   bool StartArray() {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
            current_state = IN_NESTED_CONTAINER;
            ++current_nested_container_depth;
            return true;
         case IN_NESTED_CONTAINER:
            ++current_nested_container_depth;
            return true;
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_KEY:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
         case EXPECT_TYPE_VALUE:
            return false;
         // default may not be added now, as consensus could be broken.
      }
      // Add elog to report the error and __builtin_unreachable to surpress
      // warning of control reaches end of non-void function.
      elog( "StartArray: current_state (${current_statea}) out of bound", ("current_state", current_state) );
      __builtin_unreachable();

   }
   bool EndArray(rapidjson::SizeType elementCount) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
         case EXPECT_TYPE_VALUE:
         case EXPECT_FIRST_OBJECT_KEY:
            return false;
         case IN_NESTED_CONTAINER:
            if(!--current_nested_container_depth)
               current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         // default may not be added now, as consensus could be broken.
      }
      // Add elog to report the error and __builtin_unreachable to surpress
      // warning of control reaches end of non-void function.
      elog( "EndArray: current_state (${current_statea}) out of bound", ("current_state", current_state) );
      __builtin_unreachable();
   }
};
} //detail


public_key::public_key(const signature& c, const fc::sha256& digest, bool) {
   detail::webauthn_json_handler handler;
   detail::rapidjson::Reader reader;
   detail::rapidjson::StringStream ss(c.client_json.c_str());
   FC_ASSERT(reader.Parse<detail::rapidjson::kParseIterativeFlag>(ss, handler), "Failed to parse client data JSON");

   FC_ASSERT(handler.found_type == "webauthn.get", "webauthn signature type not an assertion");

   std::vector<char> challenge_bytes = fc::base64url_decode(handler.found_challenge);
   FC_ASSERT(fc::sha256(challenge_bytes.data(), challenge_bytes.size()) == digest, "Wrong webauthn challenge");

   char required_origin_scheme[] = "https://";
   size_t https_len = strlen(required_origin_scheme);
   FC_ASSERT(handler.found_origin.compare(0, https_len, required_origin_scheme) == 0, "webauthn origin must begin with https://");
   rpid = handler.found_origin.substr(https_len, handler.found_origin.rfind(':')-https_len);

   constexpr static size_t min_auth_data_size = 37;
   FC_ASSERT(c.auth_data.size() >= min_auth_data_size, "auth_data not as large as required");
   if(c.auth_data[32] & 0x01)
      user_verification_type = user_presence_t::USER_PRESENCE_PRESENT;
   if(c.auth_data[32] & 0x04)
      user_verification_type = user_presence_t::USER_PRESENCE_VERIFIED;

   static_assert(min_auth_data_size >= sizeof(fc::sha256), "auth_data min size not enough to store a sha256");
   FC_ASSERT(memcmp(c.auth_data.data(), fc::sha256::hash(rpid).data(), sizeof(fc::sha256)) == 0, "webauthn rpid hash doesn't match origin");

   //the signature (and thus public key we need to return) will be over
   // sha256(auth_data || client_data_hash)
   fc::sha256 client_data_hash = fc::sha256::hash(c.client_json);
   fc::sha256::encoder e;
   e.write((char*)c.auth_data.data(), c.auth_data.size());
   e.write(client_data_hash.data(), client_data_hash.data_size());
   fc::sha256 signed_digest = e.result();

   int nV = c.compact_signature.data[0];
   if (nV<31 || nV>=35)
      FC_THROW_EXCEPTION( exception, "unable to reconstruct public key from signature" );
   public_key_data = r1::recover_public_key_data(c.compact_signature, signed_digest, false);
}

void public_key::post_init() {
   FC_ASSERT(rpid.length(), "webauthn pubkey must have non empty rpid");
}

}}}
