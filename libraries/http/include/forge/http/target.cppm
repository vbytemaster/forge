module;

#include <string>
#include <string_view>
#include <vector>

export module forge.http.target;

export namespace forge::http {

struct query_param {
   std::string key;
   std::string value;
   bool has_value = false;
};

struct target {
   std::string original;
   std::string path;
   std::vector<std::string> segments;
   std::string query;
   std::vector<query_param> query_params;
};

target parse_target(std::string_view value);

} // namespace forge::http
