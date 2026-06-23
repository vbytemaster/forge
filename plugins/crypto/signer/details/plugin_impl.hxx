#pragma once

namespace forge::plugins::crypto::signer {

struct plugin::impl {
   using profile_map = std::map<std::string, forge::crypto::asymmetric::encoding>;

   struct loaded_key {
      std::string key_id;
      forge::crypto::asymmetric::private_key private_key;
      std::vector<std::string> purposes;
   };

   explicit impl(plugin_options options);

   [[nodiscard]] static profile_map make_profiles(plugin_options options);
   [[nodiscard]] const forge::crypto::asymmetric::encoding& profile_by_name(std::string_view value) const;
   [[nodiscard]] response sign(request value) const;

   profile_map profiles;
   std::map<std::string, loaded_key> keys;
   std::string default_output_profile = "forge";
   bool stopping = false;
};

} // namespace forge::plugins::crypto::signer
