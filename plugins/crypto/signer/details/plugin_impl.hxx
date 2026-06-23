#pragma once

namespace fcl::plugins::crypto::signer {

struct plugin::impl {
   using profile_map = std::map<std::string, fcl::crypto::asymmetric::encoding>;

   struct loaded_key {
      std::string key_id;
      fcl::crypto::asymmetric::private_key private_key;
      std::vector<std::string> purposes;
   };

   explicit impl(plugin_options options);

   [[nodiscard]] static profile_map make_profiles(plugin_options options);
   [[nodiscard]] const fcl::crypto::asymmetric::encoding& profile_by_name(std::string_view value) const;
   [[nodiscard]] response sign(request value) const;

   profile_map profiles;
   std::map<std::string, loaded_key> keys;
   std::string default_output_profile = "fcl";
   bool stopping = false;
};

} // namespace fcl::plugins::crypto::signer
