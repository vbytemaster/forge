module;

#include <string>
#include <string_view>
#include <vector>

export module forge.plugins.http.server.bearer_auth;

export import forge.crypto.digest.sha256;
export import forge.plugins.http.server.middleware;

export namespace forge::plugins::http::server {

struct bearer_auth_options {
   std::string id = "forge.http.bearer-auth";
   int order = 0;
   std::string path_prefix = "/";
   std::vector<forge::crypto::digest::sha256> token_hashes;
};

[[nodiscard]] forge::crypto::digest::sha256 hash_bearer_token(std::string_view token);
[[nodiscard]] middleware_descriptor bearer_auth(bearer_auth_options options);

} // namespace forge::plugins::http::server
