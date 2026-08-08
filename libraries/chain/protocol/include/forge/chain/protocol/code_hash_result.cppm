module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.code_hash_result;

export import forge.chain.protocol.types;
export import forge.raw.varint_value;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct code_hash_result {
   unsigned_int struct_version;
   std::uint64_t code_sequence = 0;
   checksum256 code_hash;
   std::uint8_t vm_type = 0;
   std::uint8_t vm_version = 0;

   bool operator==(const code_hash_result&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const code_hash_result& value) {
   forge::raw::pack(stream, value.struct_version);
   forge::raw::pack(stream, value.code_sequence);
   forge::raw::pack(stream, value.code_hash);
   forge::raw::pack(stream, value.vm_type);
   forge::raw::pack(stream, value.vm_version);
}

template <typename Stream> void raw_unpack(Stream& stream, code_hash_result& value) {
   forge::raw::unpack(stream, value.struct_version);
   forge::raw::unpack(stream, value.code_sequence);
   forge::raw::unpack(stream, value.code_hash);
   forge::raw::unpack(stream, value.vm_type);
   forge::raw::unpack(stream, value.vm_version);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(code_hash_result, (), (struct_version, code_sequence, code_hash, vm_type, vm_version))
} // namespace forge::chain::protocol
#endif
