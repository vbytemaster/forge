module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <bit>
#include <cstdint>
#include <deque>
#include <new>
#include <optional>
#include <utility>
#include <variant>

export module forge.chain.protocol.block;

export import forge.raw.varint;

export import forge.chain.protocol.producer_schedule;
export import forge.chain.protocol.transaction;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain::protocol {

struct block_header {
   block_timestamp timestamp;
   account_name producer;
   std::uint16_t confirmed = 1;
   block_id previous;
   checksum256 transaction_mroot;
   checksum256 action_mroot;
   std::uint32_t schedule_version = 0;
   std::optional<producer_schedule> new_producers;
   extensions header_extensions;

   [[nodiscard]] core::digest digest() const;
   [[nodiscard]] block_id calculate_id() const;
   [[nodiscard]] std::uint32_t calculate_block_num() const;
   [[nodiscard]] static std::uint32_t num_from_id(const block_id& id);
};

struct signed_block_header : block_header {
   signature producer_signature;
};

struct transaction_receipt_header {
   enum class status : std::uint8_t {
      executed = 0,
      soft_fail = 1,
      hard_fail = 2,
      delayed = 3,
      expired = 4,
   };
   BOOST_DESCRIBE_NESTED_ENUM(status, executed, soft_fail, hard_fail, delayed, expired)

   status status = status::hard_fail;
   std::uint32_t cpu_usage_us = 0;
   forge::unsigned_int net_usage_words = 0;
};

struct transaction_receipt : transaction_receipt_header {
   std::variant<transaction_id, packed_transaction> trx;

   [[nodiscard]] core::digest digest() const;
};

struct signed_block : signed_block_header {
   std::deque<transaction_receipt> transactions;
   extensions block_extensions;

   [[nodiscard]] core::digest packed_digest() const;
};

struct producer_confirmation {
   ::forge::chain::protocol::block_id block_id;
   core::digest block_digest;
   account_name producer;
   signature sig;
};

bytes signature_preimage(const block_header& value);
core::digest block_digest(const block_header& value);
block_id calculate_block_id(const block_header& value);
std::uint32_t calculate_block_num_from_id(const block_id& id);
std::uint32_t calculate_block_num(const block_header& value);
core::digest transaction_receipt_digest(const transaction_receipt& value);
core::digest calculate_transaction_mroot(const std::deque<transaction_receipt>& receipts);
core::digest signed_block_digest(const signed_block& value);

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(block_header, (),
                      (timestamp, producer, confirmed, previous, transaction_mroot, action_mroot, schedule_version,
                       new_producers, header_extensions))
BOOST_DESCRIBE_STRUCT(signed_block_header, (block_header), (producer_signature))
BOOST_DESCRIBE_STRUCT(transaction_receipt_header, (), (status, cpu_usage_us, net_usage_words))
BOOST_DESCRIBE_STRUCT(transaction_receipt, (transaction_receipt_header), (trx))
BOOST_DESCRIBE_STRUCT(signed_block, (signed_block_header), (transactions, block_extensions))
BOOST_DESCRIBE_STRUCT(producer_confirmation, (), (block_id, block_digest, producer, sig))
} // namespace forge::chain::protocol

export namespace forge::raw {

template <> struct enum_wire_type<decltype(std::declval<forge::chain::protocol::transaction_receipt_header>().status)> {
   using type = std::uint8_t;
};

template <typename Stream> void pack(Stream& stream, const forge::chain::protocol::signed_block& value) {
   forge::raw::pack(stream, static_cast<const forge::chain::protocol::signed_block_header&>(value));
   forge::raw::pack(stream, value.transactions);
   forge::raw::pack(stream, value.block_extensions);
}

template <typename Stream> void unpack(Stream& stream, forge::chain::protocol::signed_block& value) {
   forge::raw::unpack(stream, static_cast<forge::chain::protocol::signed_block_header&>(value));
   forge::raw::unpack(stream, value.transactions);
   forge::raw::unpack(stream, value.block_extensions);
}

template <>
inline void pack<forge::datastream<std::size_t>, forge::chain::protocol::signed_block>(
    forge::datastream<std::size_t>& stream, const forge::chain::protocol::signed_block& value) {
   forge::raw::pack(stream, static_cast<const forge::chain::protocol::signed_block_header&>(value));
   forge::raw::pack(stream, value.transactions);
   forge::raw::pack(stream, value.block_extensions);
}

template <>
inline void pack<forge::datastream<std::uint8_t*>, forge::chain::protocol::signed_block>(
    forge::datastream<std::uint8_t*>& stream, const forge::chain::protocol::signed_block& value) {
   forge::raw::pack(stream, static_cast<const forge::chain::protocol::signed_block_header&>(value));
   forge::raw::pack(stream, value.transactions);
   forge::raw::pack(stream, value.block_extensions);
}

template <>
inline void unpack<forge::datastream<const std::uint8_t*>, forge::chain::protocol::signed_block>(
    forge::datastream<const std::uint8_t*>& stream, forge::chain::protocol::signed_block& value) {
   forge::raw::unpack(stream, static_cast<forge::chain::protocol::signed_block_header&>(value));
   forge::raw::unpack(stream, value.transactions);
   forge::raw::unpack(stream, value.block_extensions);
}

inline forge::chain::protocol::bytes pack(const forge::chain::protocol::signed_block& value) {
   forge::datastream<std::size_t> size_stream;
   forge::raw::pack(size_stream, value);

   forge::chain::protocol::bytes out(size_stream.tellp());
   if (!out.empty()) {
      forge::datastream<std::uint8_t*> stream(out.data(), out.size());
      forge::raw::pack(stream, value);
   }
   return out;
}

} // namespace forge::raw

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::block_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::signed_block_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::transaction_receipt_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::transaction_receipt)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::producer_confirmation)
