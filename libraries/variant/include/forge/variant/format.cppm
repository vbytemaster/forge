module;
#include <string>

export module forge.variant.format;

import forge.variant.value;

export namespace forge {
std::string format_string(const std::string& format, const variant_object& args, bool minimize = false);
}
