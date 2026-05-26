module;
#include <fcl/exception/macros.hpp>
#include <string>
export module fcl.crypto.bls_common;

import fcl.crypto.base64;
import fcl.crypto.common;
import fcl.exception.exception;
import fcl.raw.datastream;
import fcl.raw.raw;

export namespace fcl::crypto::blslib {

template <typename Container> Container deserialize_base64url(const std::string& data_str) {
   using wrapper = checksummed_data<Container>;
   wrapper wrapped;

   auto bin = fcl::crypto::base64url_decode(data_str);
   fcl::datastream<const char*> unpacker(bin.data(), bin.size());
   fcl::raw::unpack(unpacker, wrapped);
   FCL_ASSERT(!unpacker.remaining(), "decoded base64url length too long");
   auto checksum = wrapper::calculate_checksum(wrapped.data, nullptr);
   FCL_ASSERT(checksum == wrapped.check);

   return wrapped.data;
}

template <typename Container> std::string serialize_base64url(const Container& data) {
   using wrapper = checksummed_data<Container>;
   wrapper wrapped;

   wrapped.data = data;
   wrapped.check = wrapper::calculate_checksum(wrapped.data, nullptr);
   auto packed = raw::pack(wrapped);
   auto data_str = fcl::crypto::base64url_encode(packed.data(), packed.size());

   return data_str;
}

} // namespace fcl::crypto::blslib
