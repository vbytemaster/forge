import forge.core.chrono;

#include <chrono>
#include <string>

int main()
{
   const auto instant = std::chrono::sys_seconds{std::chrono::seconds{1}};
   const auto text = forge::chrono::to_iso_string(instant);
   return text.empty() ? 1 : 0;
}
