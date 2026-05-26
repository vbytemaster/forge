module;
#include <cstdint>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <string>

module fcl.crypto.rand;

import fcl.crypto.exceptions;
import fcl.crypto.openssl;

namespace fcl::crypto {

void rand_bytes(char* buf, int count) {
   int result = RAND_bytes((unsigned char*)buf, count);
   if (result != 1)
      exceptions::raise(exceptions::code::backend_error,
                        "Error calling OpenSSL's RAND_bytes(): " +
                           std::to_string(static_cast<uint32_t>(ERR_get_error())));
}

void rand_pseudo_bytes(char* buf, int count) {
   rand_bytes(buf, count);
}

} // namespace fcl::crypto
