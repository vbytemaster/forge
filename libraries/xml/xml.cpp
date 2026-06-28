module;

#include <pugixml.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

module forge.xml;

namespace forge::xml {
namespace {

struct tree_metrics {
   std::size_t attributes = 0;
   std::size_t children = 0;
};

[[nodiscard]] bool starts_with_xml_declaration(std::string_view input) {
   if (!input.starts_with("<?xml")) {
      return false;
   }
   if (input.size() <= 5) {
      return false;
   }
   const auto next = input[5];
   return next == ' ' || next == '\t' || next == '\r' || next == '\n';
}

[[nodiscard]] bool has_non_whitespace_text(std::string_view input) {
   return std::ranges::any_of(input, [](char value) {
      return std::isspace(static_cast<unsigned char>(value)) == 0;
   });
}

[[nodiscard]] std::optional<schema::diagnostic> reject_unsafe_xml(std::string_view input,
                                                                  const read_options& options) {
   auto scan = input;
   if (starts_with_xml_declaration(scan)) {
      if (const auto end = scan.find("?>"); end != std::string_view::npos) {
         scan.remove_prefix(end + 2);
      }
   }

   if (scan.find("<?") != std::string_view::npos || input.find("<!DOCTYPE") != std::string_view::npos ||
       input.find("<!ENTITY") != std::string_view::npos || input.find("<!--") != std::string_view::npos) {
      return detail::make_error(options.source_name, "xml.unsafe", "XML input contains disabled XML features");
   }
   return std::nullopt;
}

[[nodiscard]] std::optional<schema::diagnostic> convert_node(const pugi::xml_node& node,
                                                             element& output,
                                                             const read_options& options,
                                                             tree_metrics& metrics,
                                                             std::size_t depth) {
   if (depth > options.max_depth) {
      return detail::make_error(output.name, "xml.depth", "XML input exceeds configured maximum depth");
   }

   output.name = node.name();
   for (const auto& attribute : node.attributes()) {
      ++metrics.attributes;
      if (metrics.attributes > options.max_attributes) {
         return detail::make_error(output.name, "xml.attributes", "XML input has too many attributes");
      }
      output.attributes.push_back({.name = attribute.name(), .value = attribute.value()});
   }

   for (const auto& child : node.children()) {
      switch (child.type()) {
      case pugi::node_element: {
         if (!output.text.empty()) {
            if (has_non_whitespace_text(output.text)) {
               return detail::make_error(output.name,
                                         "xml.mixed_content",
                                         "XML mixed text and child element content is not supported");
            }
            output.text.clear();
         }
         ++metrics.children;
         if (metrics.children > options.max_children) {
            return detail::make_error(output.name, "xml.children", "XML input has too many child elements");
         }
         auto converted = element{};
         if (auto error = convert_node(child, converted, options, metrics, depth + 1)) {
            return error;
         }
         output.children.push_back(std::move(converted));
         break;
      }
      case pugi::node_pcdata:
      case pugi::node_cdata:
         if (!output.children.empty()) {
            if (has_non_whitespace_text(child.value())) {
               return detail::make_error(output.name,
                                         "xml.mixed_content",
                                         "XML mixed text and child element content is not supported");
            }
            break;
         }
         output.text += child.value();
         if (output.text.size() > options.max_text_bytes) {
            return detail::make_error(output.name, "xml.text", "XML text exceeds configured byte limit");
         }
         break;
      case pugi::node_null:
         break;
      default:
         return detail::make_error(output.name, "xml.unsafe", "XML input contains disabled XML node types");
      }
   }

   return std::nullopt;
}

void append_node(pugi::xml_node parent, const element& input) {
   auto node = parent.append_child(input.name.c_str());
   for (const auto& attribute : input.attributes) {
      node.append_attribute(attribute.name.c_str()).set_value(attribute.value.c_str());
   }
   if (!input.text.empty()) {
      node.append_child(pugi::node_pcdata).set_value(input.text.c_str());
   }
   for (const auto& child : input.children) {
      append_node(node, child);
   }
}

[[nodiscard]] bool has_attribute(const element& input, std::string_view name) {
   for (const auto& attribute : input.attributes) {
      if (attribute.name == name) {
         return true;
      }
   }
   return false;
}

} // namespace

read_result<document> read_value(std::string_view input, read_options options) {
   auto result = read_result<document>{};
   if (input.size() > options.max_bytes) {
      result.diagnostics.push_back(
         detail::make_error(options.source_name, "xml.input-bytes", "XML input exceeds configured byte limit"));
      return result;
   }
   if (auto unsafe = reject_unsafe_xml(input, options)) {
      result.diagnostics.push_back(std::move(*unsafe));
      return result;
   }

   auto parsed = pugi::xml_document{};
   const auto status = parsed.load_buffer(input.data(), input.size(), pugi::parse_default, pugi::encoding_utf8);
   if (!status) {
      result.diagnostics.push_back(
         detail::make_error(options.source_name, "xml.parse", status.description()));
      return result;
   }

   auto root_count = 0U;
   auto metrics = tree_metrics{};
   for (const auto& node : parsed.children()) {
      if (node.type() != pugi::node_element) {
         continue;
      }
      ++root_count;
      if (root_count > 1U) {
         result.diagnostics.push_back(
            detail::make_error(options.source_name, "xml.document", "XML document has multiple root elements"));
         return result;
      }
      if (auto error = convert_node(node, result.value.root, options, metrics, 1)) {
         result.diagnostics.push_back(std::move(*error));
         return result;
      }
   }

   if (root_count == 0U) {
      result.diagnostics.push_back(
         detail::make_error(options.source_name, "xml.document", "XML document has no root element"));
   }
   return result;
}

write_result write_value(const document& input, write_options options) {
   auto result = write_result{};
   if (std::chrono::system_clock::now() > options.deadline) {
      result.diagnostics.push_back(detail::make_error({}, "xml.deadline", "XML write deadline expired"));
      return result;
   }

   auto root = input.root;
   if (!options.root_name.empty()) {
      root.name = options.root_name;
   }
   if (root.name.empty()) {
      result.diagnostics.push_back(detail::make_error({}, "xml.write", "XML root element name is empty"));
      return result;
   }
   if (!options.default_namespace.empty() && !has_attribute(root, "xmlns")) {
      root.attributes.insert(root.attributes.begin(), {.name = "xmlns", .value = options.default_namespace});
   }

   auto doc = pugi::xml_document{};
   if (options.xml_declaration) {
      auto declaration = doc.append_child(pugi::node_declaration);
      declaration.append_attribute("version").set_value("1.0");
      declaration.append_attribute("encoding").set_value("UTF-8");
   }
   append_node(doc, root);

   auto stream = std::ostringstream{};
   doc.save(stream, options.pretty ? "  " : "", options.pretty ? pugi::format_default : pugi::format_raw,
            pugi::encoding_utf8);
   auto text = std::move(stream).str();
   if (text.size() > options.max_bytes) {
      result.diagnostics.push_back(
         detail::make_error({}, "xml.max-bytes", "XML output exceeds configured byte limit"));
      return result;
   }

   result.text = std::move(text);
   return result;
}

} // namespace forge::xml
