module;
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <sstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

module forge.core.string;

import forge.core.utf8;
/**
 *  Implemented with std::string for now.
 */

namespace forge {
class comma_numpunct : public std::numpunct<char> {
 protected:
   virtual char do_thousands_sep() const {
      return ',';
   }
   virtual std::string do_grouping() const {
      return "\03";
   }
};

int64_t to_int64(const std::string& i) {
   try {
      return boost::lexical_cast<int64_t>(i.c_str(), i.size());
   } catch (const boost::bad_lexical_cast& e) {
      throw std::invalid_argument("could not parse int64_t: " + i);
   }
}

uint64_t to_uint64(const std::string& i) {
   try {
      return boost::lexical_cast<uint64_t>(i.c_str(), i.size());
   } catch (const boost::bad_lexical_cast& e) {
      throw std::invalid_argument("could not parse uint64_t: " + i);
   }
}

double to_double(const std::string& i) {
   try {
      return boost::lexical_cast<double>(i.c_str(), i.size());
   } catch (const boost::bad_lexical_cast& e) {
      throw std::invalid_argument("could not parse double: " + i);
   }
}

static void append_json_escaped_char(std::string& out, unsigned char c, bool escape_ctrl) {
   switch (c) {
   case '\\':
      out += escape_ctrl ? "\\\\" : "\\";
      return;
   case '"':
      out += escape_ctrl ? "\\\"" : "\"";
      return;
   case '\t':
      out += escape_ctrl ? "\\t" : "\t";
      return;
   case '\r':
      out += escape_ctrl ? "\\r" : "\r";
      return;
   case '\n':
      out += escape_ctrl ? "\\n" : "\n";
      return;
   default:
      if (c == 0x7f || c < 0x20) {
         char buf[7] = {};
         std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
         out += buf;
      } else {
         out.push_back(static_cast<char>(c));
      }
   }
}

static std::string escape_for_string_boundary(const std::string_view str, bool escape_ctrl) {
   const auto pruned = forge::prune_invalid_utf8(str);
   std::string out;
   out.reserve(pruned.size());
   for (unsigned char c : pruned) {
      append_json_escaped_char(out, c, escape_ctrl);
   }
   return out;
}

std::pair<std::string&, bool> escape_str(std::string& str, escape_control_chars escape_ctrl, std::size_t max_len,
                                         std::string_view add_truncate_str) {
   bool modified = false, truncated = false;
   // truncate early to speed up escape
   if (str.size() > max_len) {
      str.resize(max_len);
      modified = truncated = true;
   }
   auto itr = escape_ctrl == escape_control_chars::on
                  ? std::find_if(str.begin(), str.end(),
                                 [](const auto& c) {
                                    return c == '\x7f' || c == '\\' || c == '\"' || (c >= '\x00' && c <= '\x1f');
                                 })
                  : std::find_if(str.begin(), str.end(),
                                 [](const auto& c) { // x09 = \t, x0a = \n,                   x0d = \r
                                    return c == '\x7f' || (c >= '\x00' && c <= '\x08') || c == '\x0b' || c == '\x0c' ||
                                           (c >= '\x0e' && c <= '\x1f');
                                 });

   if (itr != str.end() || !forge::is_valid_utf8(str)) {
      str = escape_for_string_boundary(str, escape_ctrl == escape_control_chars::on);
      modified = true;
      if (str.size() > max_len) {
         str.resize(max_len);
         truncated = true;
      }
   }

   if (truncated) {
      str += add_truncate_str;
   }

   return std::make_pair(std::ref(str), modified);
}

} // namespace forge
