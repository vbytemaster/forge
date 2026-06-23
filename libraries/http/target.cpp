module;

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/url.hpp>

module forge.http.target;

namespace forge::http {

target parse_target(std::string_view value) {
   const auto parsed = boost::urls::parse_origin_form(value);
   if (!parsed.has_value()) {
      throw std::invalid_argument{"invalid HTTP request target"};
   }

   const auto& url = parsed.value();
   auto result = target{
       .original = std::string{value},
       .path = url.path(),
       .query = url.query(),
   };

   for (const auto segment : url.segments()) {
      result.segments.emplace_back(segment);
   }
   for (const auto param : url.params()) {
      result.query_params.push_back(query_param{
          .key = param.key,
          .value = param.value,
          .has_value = param.has_value,
      });
   }

   return result;
}

} // namespace forge::http
