module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

module forge.variant.format;

import forge.variant.value;

namespace {
constexpr size_t minimize_max_size = 1024;

void append_json_string(std::string& out, std::string_view value) {
   out.push_back('"');
   for (const char c : value) {
      switch (c) {
      case '"':
         out += "\\\"";
         break;
      case '\\':
         out += "\\\\";
         break;
      case '\b':
         out += "\\b";
         break;
      case '\f':
         out += "\\f";
         break;
      case '\n':
         out += "\\n";
         break;
      case '\r':
         out += "\\r";
         break;
      case '\t':
         out += "\\t";
         break;
      default:
         if (static_cast<unsigned char>(c) < 0x20) {
            constexpr char hex[] = "0123456789abcdef";
            out += "\\u00";
            out.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0x0f]);
            out.push_back(hex[static_cast<unsigned char>(c) & 0x0f]);
         } else {
            out.push_back(c);
         }
      }
   }
   out.push_back('"');
}

void append_variant_json(std::string& out, const forge::variant& value);

void append_object_json(std::string& out, const forge::variant_object& object) {
   out.push_back('{');
   bool first = true;
   for (auto itr = object.begin(); itr != object.end(); ++itr) {
      if (!first) {
         out.push_back(',');
      }
      first = false;
      append_json_string(out, itr->key());
      out.push_back(':');
      append_variant_json(out, itr->value());
   }
   out.push_back('}');
}

void append_array_json(std::string& out, const forge::variants& values) {
   out.push_back('[');
   for (std::size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
         out.push_back(',');
      }
      append_variant_json(out, values[i]);
   }
   out.push_back(']');
}

void append_variant_json(std::string& out, const forge::variant& value) {
   switch (value.get_type()) {
   case forge::variant::null_type:
      out += "null";
      break;
   case forge::variant::int64_type:
   case forge::variant::uint64_type:
   case forge::variant::double_type:
      out += value.as_string();
      break;
   case forge::variant::bool_type:
      out += value.as_bool() ? "true" : "false";
      break;
   case forge::variant::string_type:
      append_json_string(out, value.get_string());
      break;
   case forge::variant::array_type:
      append_array_json(out, value.get_array());
      break;
   case forge::variant::object_type:
      append_object_json(out, value.get_object());
      break;
   case forge::variant::blob_type:
      append_json_string(out, value.as_string());
      break;
   }
}

std::string variant_json_string(const forge::variant& value) {
   std::string out;
   append_variant_json(out, value);
   return out;
}

void clean_append(std::string& app, const std::string_view& s, size_t pos = 0, size_t len = std::string::npos) {
   std::string_view sub = s.substr(pos, len);
   app.reserve(app.size() + sub.size());
   app += sub;
}
} // namespace

namespace forge {
std::string format_string(const std::string& frmt, const variant_object& args, bool minimize) {
   std::string result;
   const std::string format =
       (minimize && frmt.size() > minimize_max_size) ? frmt.substr(0, minimize_max_size) + "..." : frmt;

   const auto arg_num = (args.size() == 0) ? 1 : args.size();
   const auto max_format_size = std::max(minimize_max_size, format.size());
   const auto minimize_sub_max_size = minimize ? (max_format_size - format.size()) / arg_num : minimize_max_size;
   result.reserve(max_format_size + 3 * args.size());

   size_t prev = 0;
   size_t next = format.find('$');
   while (prev != std::string::npos && prev < format.size()) {
      if (next != std::string::npos) {
         clean_append(result, format, prev, next - prev);
      } else {
         clean_append(result, format, prev);
      }

      if (next == std::string::npos) {
         return result;
      }
      if (minimize && result.size() > minimize_max_size) {
         result += "...";
         return result;
      }

      prev = next + 1;
      if (format[prev] == '{') {
         next = format.find('}', prev);
         if (next != std::string::npos) {
            std::string key = format.substr(prev + 1, next - prev - 1);
            auto val = args.find(key);
            bool replaced = true;
            if (val != args.end()) {
               if (val->value().is_object() || val->value().is_array()) {
                  if (minimize && result.size() >= minimize_max_size) {
                     replaced = false;
                  } else {
                     const auto max_length = minimize ? minimize_sub_max_size : std::numeric_limits<uint64_t>::max();
                     const auto encoded = variant_json_string(val->value());
                     if (minimize && encoded.size() > max_length) {
                        replaced = false;
                     } else {
                        clean_append(result, encoded);
                     }
                  }
               } else if (val->value().is_blob()) {
                  if (minimize && val->value().get_blob().data.size() > minimize_sub_max_size) {
                     replaced = false;
                  } else {
                     clean_append(result, val->value().as_string());
                  }
               } else if (val->value().is_string()) {
                  if (minimize && val->value().get_string().size() > minimize_sub_max_size) {
                     auto sz = std::min(minimize_sub_max_size, minimize_max_size - result.size());
                     clean_append(result, val->value().get_string(), 0, sz);
                     result += "...";
                  } else {
                     clean_append(result, val->value().get_string());
                  }
               } else {
                  clean_append(result, val->value().as_string());
               }
            } else {
               replaced = false;
            }
            if (!replaced) {
               result += "${";
               clean_append(result, key);
               result += "}";
            }
            prev = next + 1;
            next = format.find('$', prev);
         }
      } else {
         clean_append(result, format, prev, 1);
         ++prev;
         next = format.find('$', prev);
      }
   }
   return result;
}
} // namespace forge
