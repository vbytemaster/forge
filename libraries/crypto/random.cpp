module;

#include <fcl/exceptions/macros.hpp>

#include <openssl/rand.h>

#include <cstdint>
#include <limits>
#include <span>

module fcl.crypto.random;

namespace fcl::crypto {
namespace {

void require_rand_size(std::size_t size) {
   if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      FCL_THROW_EXCEPTION(random::exceptions::invalid_options, "random byte request is too large");
   }
}

} // namespace

void fill_random(std::span<std::uint8_t> out) {
   require_rand_size(out.size());
   if (!out.empty() && RAND_bytes(out.data(), static_cast<int>(out.size())) != 1) {
      FCL_THROW_EXCEPTION(random::exceptions::backend_error, "OpenSSL RAND_bytes failed");
   }
}

bytes random_bytes(std::size_t size) {
   auto out = bytes(size);
   fill_random(out);
   return out;
}

} // namespace fcl::crypto
