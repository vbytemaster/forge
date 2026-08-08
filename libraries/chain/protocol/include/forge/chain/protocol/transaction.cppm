module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>
#endif

#include <cstdint>
#include <utility>
#include <vector>

export module forge.chain.protocol.transaction;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.raw.varint;

export namespace forge::chain::protocol {

struct signed_transaction : transaction {
   std::vector<signature> signatures;
   std::vector<bytes> context_free_data;
};

struct packed_transaction {
   enum class compression : std::uint8_t {
      none = 0,
      zlib = 1,
   };
   BOOST_DESCRIBE_NESTED_ENUM(compression, none, zlib)

   packed_transaction() = default;
   explicit packed_transaction(const signed_transaction& value, compression compression = compression::none);
   explicit packed_transaction(signed_transaction&& value, compression compression = compression::none);

   std::vector<signature> signatures;
   compression compression = compression::none;
   bytes packed_context_free_data;
   bytes packed_trx;

   [[nodiscard]] digest packed_digest() const;
   [[nodiscard]] transaction_id id() const;
   [[nodiscard]] signed_transaction get_signed_transaction() const;
};

bytes pack_transaction(const transaction& value);
transaction_id calculate_transaction_id(const transaction& value);
bytes signature_preimage(const chain_id& chain_id, const transaction& value, const std::vector<bytes>& cfd = {});
digest signature_digest(const chain_id& chain_id, const transaction& value, const std::vector<bytes>& cfd = {});

template <typename Stream> void raw_pack(Stream& stream, const signed_transaction& value) {
   raw_pack(stream, static_cast<const transaction&>(value));
   forge::raw::pack(stream, value.signatures);
   forge::raw::pack(stream, value.context_free_data);
}

template <typename Stream> void raw_unpack(Stream& stream, signed_transaction& value) {
   raw_unpack(stream, static_cast<transaction&>(value));
   forge::raw::unpack(stream, value.signatures);
   forge::raw::unpack(stream, value.context_free_data);
}

template <typename Stream> void raw_pack(Stream& stream, const packed_transaction& value) {
   forge::raw::pack(stream, value.signatures);
   forge::raw::pack(stream, value.compression);
   forge::raw::pack(stream, value.packed_context_free_data);
   forge::raw::pack(stream, value.packed_trx);
}

template <typename Stream> void raw_unpack(Stream& stream, packed_transaction& value) {
   forge::raw::unpack(stream, value.signatures);
   forge::raw::unpack(stream, value.compression);
   forge::raw::unpack(stream, value.packed_context_free_data);
   forge::raw::unpack(stream, value.packed_trx);
}

} // namespace forge::chain::protocol

export namespace forge::raw {

template <> struct enum_wire_type<decltype(std::declval<forge::chain::protocol::packed_transaction>().compression)> {
   using type = std::uint8_t;
};

} // namespace forge::raw

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(deferred_transaction_generation_context, (), (sender_trx_id, sender_id, sender))
BOOST_DESCRIBE_STRUCT(transaction_header, (),
                      (expiration, ref_block_num, ref_block_prefix, max_net_usage_words, max_cpu_usage_ms, delay_sec))
BOOST_DESCRIBE_STRUCT(transaction, (transaction_header), (context_free_actions, actions, transaction_extensions))
BOOST_DESCRIBE_STRUCT(signed_transaction, (transaction), (signatures, context_free_data))
BOOST_DESCRIBE_STRUCT(packed_transaction, (), (signatures, compression, packed_context_free_data, packed_trx))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::deferred_transaction_generation_context)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::transaction_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::transaction)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::signed_transaction)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::packed_transaction)
#endif
