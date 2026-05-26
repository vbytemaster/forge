module;

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rocksdb/db.h>
#include <rocksdb/options.h>

module fcl.p2p.peer_store;

import fcl.p2p.exceptions;

namespace fcl::p2p {
namespace {

constexpr auto peer_store_magic = std::string_view{"FCLP2PPS"};
constexpr auto peer_store_version = std::uint8_t{2};

[[nodiscard]] bool same_endpoint(const fcl::quic::endpoint& left, const fcl::quic::endpoint& right) {
   return left.host == right.host && left.port == right.port;
}

void refresh_endpoint_score(peer_store::endpoint_record& endpoint) {
   endpoint.score = score_path(path::observation{
       .kind = endpoint.kind,
       .latency = endpoint.last_latency,
       .failures = endpoint.failures,
       .successes = endpoint.successes,
       .last_success = endpoint.successes > 0 && endpoint.failures == 0,
   });
}

void refresh_record_score(peer_store::record& record, path::kind kind, bool last_success) {
   record.score = score_path(path::observation{
       .kind = kind,
       .latency = record.last_latency,
       .failures = record.failures,
       .successes = record.successes,
       .last_success = last_success,
   });
}

void expire_reachability(peer_store::record& record, std::chrono::system_clock::time_point now) {
   if (record.reachability_expires_at == std::chrono::system_clock::time_point{} ||
       record.reachability_expires_at > now) {
      return;
   }
   record.reachability = reachability::state::unknown;
   record.observed_endpoint.reset();
   record.reachability_expires_at = {};
}

void normalize_for_storage(peer_store::record& value) {
   for (auto& endpoint : value.endpoints) {
      refresh_endpoint_score(endpoint);
   }
   const auto kind = value.endpoints.empty() ? path::kind::direct : value.endpoints.front().kind;
   refresh_record_score(value, kind, value.successes > 0);
}

class binary_writer {
 public:
   void raw(std::string_view value) {
      data_.append(value);
   }

   void u8(std::uint8_t value) {
      data_.push_back(static_cast<char>(value));
   }

   void boolean(bool value) {
      u8(value ? 1 : 0);
   }

   void u16(std::uint16_t value) {
      u64(value);
   }

   void u64(std::uint64_t value) {
      for (auto i = 0U; i < 8U; ++i) {
         data_.push_back(static_cast<char>((value >> (i * 8U)) & 0xffU));
      }
   }

   void i64(std::int64_t value) {
      u64(static_cast<std::uint64_t>(value));
   }

   void f64(double value) {
      auto bits = std::uint64_t{};
      std::memcpy(&bits, &value, sizeof(bits));
      u64(bits);
   }

   void text(std::string_view value) {
      u64(value.size());
      data_.append(value);
   }

   void bytes(const std::vector<std::uint8_t>& value) {
      u64(value.size());
      for (auto byte : value) {
         data_.push_back(static_cast<char>(byte));
      }
   }

   void time(std::chrono::system_clock::time_point value) {
      if (value == std::chrono::system_clock::time_point{}) {
         i64(0);
         return;
      }
      i64(std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
   }

   void endpoint(const fcl::quic::endpoint& value) {
      text(value.host);
      u16(value.port);
   }

   void optional_endpoint(const std::optional<fcl::quic::endpoint>& value) {
      boolean(value.has_value());
      if (value) {
         endpoint(*value);
      }
   }

   void peer(const peer_id& value) {
      text(value.value);
   }

   void optional_peer(const std::optional<peer_id>& value) {
      boolean(value.has_value());
      if (value) {
         peer(*value);
      }
   }

   [[nodiscard]] std::string finish() && {
      return std::move(data_);
   }

 private:
   std::string data_;
};

class binary_reader {
 public:
   explicit binary_reader(std::string_view data) : data_(data) {}

   void expect_raw(std::string_view expected) {
      if (data_.substr(position_, expected.size()) != expected) {
         exceptions::raise(exceptions::code::codec_error, "invalid RocksDB peer store record header");
      }
      position_ += expected.size();
   }

   [[nodiscard]] std::uint8_t u8() {
      require(1);
      return static_cast<std::uint8_t>(data_[position_++]);
   }

   [[nodiscard]] bool boolean() {
      const auto value = u8();
      if (value > 1U) {
         exceptions::raise(exceptions::code::codec_error, "invalid RocksDB peer store boolean");
      }
      return value != 0U;
   }

   [[nodiscard]] std::uint64_t u64() {
      require(8);
      auto value = std::uint64_t{};
      for (auto i = 0U; i < 8U; ++i) {
         value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[position_++])) << (i * 8U);
      }
      return value;
   }

   [[nodiscard]] std::int64_t i64() {
      return static_cast<std::int64_t>(u64());
   }

   [[nodiscard]] double f64() {
      const auto bits = u64();
      auto value = double{};
      std::memcpy(&value, &bits, sizeof(value));
      return value;
   }

   [[nodiscard]] std::string text() {
      const auto size = checked_size(u64());
      require(size);
      auto out = std::string{data_.substr(position_, size)};
      position_ += size;
      return out;
   }

   [[nodiscard]] std::vector<std::uint8_t> bytes() {
      const auto size = checked_size(u64());
      require(size);
      auto out = std::vector<std::uint8_t>{};
      out.reserve(size);
      for (auto i = std::size_t{}; i < size; ++i) {
         out.push_back(static_cast<std::uint8_t>(data_[position_++]));
      }
      return out;
   }

   [[nodiscard]] std::chrono::system_clock::time_point time() {
      const auto milliseconds = i64();
      if (milliseconds == 0) {
         return {};
      }
      return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
   }

   [[nodiscard]] fcl::quic::endpoint endpoint() {
      const auto host = text();
      const auto port_value = u64();
      if (port_value > std::numeric_limits<std::uint16_t>::max()) {
         exceptions::raise(exceptions::code::codec_error, "invalid RocksDB peer store endpoint port");
      }
      return fcl::quic::endpoint{.host = host, .port = static_cast<std::uint16_t>(port_value)};
   }

   [[nodiscard]] std::optional<fcl::quic::endpoint> optional_endpoint() {
      if (!boolean()) {
         return std::nullopt;
      }
      return endpoint();
   }

   [[nodiscard]] peer_id peer() {
      return peer_id{.value = text()};
   }

   [[nodiscard]] std::optional<peer_id> optional_peer() {
      if (!boolean()) {
         return std::nullopt;
      }
      return peer();
   }

   void finish() const {
      if (position_ != data_.size()) {
         exceptions::raise(exceptions::code::codec_error, "trailing bytes in RocksDB peer store record");
      }
   }

 private:
   [[nodiscard]] std::size_t checked_size(std::uint64_t value) const {
      if (value > static_cast<std::uint64_t>(data_.size() - position_)) {
         exceptions::raise(exceptions::code::codec_error, "truncated RocksDB peer store record");
      }
      return static_cast<std::size_t>(value);
   }

   void require(std::size_t size) const {
      if (size > data_.size() - position_) {
         exceptions::raise(exceptions::code::codec_error, "truncated RocksDB peer store record");
      }
   }

   std::string_view data_;
   std::size_t position_ = 0;
};

[[nodiscard]] std::string encode_record(const peer_store::record& value) {
   auto writer = binary_writer{};
   writer.raw(peer_store_magic);
   writer.u8(peer_store_version);
   writer.peer(value.peer);
   writer.u64(value.capabilities.bits);
   writer.text(value.protocol_version);
   writer.text(value.agent_version);
   writer.bytes(value.public_key);
   writer.u64(value.protocols.size());
   for (const auto& protocol : value.protocols) {
      writer.text(protocol.value);
   }
   writer.bytes(value.signed_peer_record);
   writer.u64(value.endpoints.size());
   for (const auto& endpoint : value.endpoints) {
      writer.endpoint(endpoint.endpoint);
      writer.u64(static_cast<std::uint64_t>(endpoint.kind));
      writer.optional_peer(endpoint.relay_peer);
      writer.u64(endpoint.successes);
      writer.u64(endpoint.failures);
      writer.i64(endpoint.last_latency.count());
      writer.time(endpoint.backoff_until);
      writer.f64(endpoint.score);
   }
   writer.u64(value.relay_reservations.size());
   for (const auto& reservation : value.relay_reservations) {
      writer.peer(reservation.relay);
      writer.u64(reservation.reservation_id);
      writer.time(reservation.expires_at);
      writer.u64(reservation.endpoints.size());
      for (const auto& endpoint : reservation.endpoints) {
         writer.endpoint(endpoint);
      }
      writer.bytes(reservation.voucher);
      writer.u64(reservation.successes);
      writer.u64(reservation.failures);
      writer.i64(reservation.last_latency.count());
      writer.f64(reservation.score);
   }
   writer.u64(static_cast<std::uint64_t>(value.reachability));
   writer.optional_endpoint(value.observed_endpoint);
   writer.time(value.reachability_expires_at);
   writer.u64(value.successes);
   writer.u64(value.failures);
   writer.i64(value.last_latency.count());
   writer.f64(value.score);
   return std::move(writer).finish();
}

[[nodiscard]] peer_store::record decode_record(std::string_view data) {
   auto reader = binary_reader{data};
   reader.expect_raw(peer_store_magic);
   const auto version = reader.u8();
   if (version != peer_store_version) {
      exceptions::raise(exceptions::code::codec_error, "unsupported RocksDB peer store record version");
   }

   auto value = peer_store::record{};
   value.peer = reader.peer();
   value.capabilities.bits = reader.u64();
   value.protocol_version = reader.text();
   value.agent_version = reader.text();
   value.public_key = reader.bytes();

   const auto protocol_count = reader.u64();
   for (auto i = std::uint64_t{}; i < protocol_count; ++i) {
      value.protocols.push_back(protocol_id{.value = reader.text()});
   }

   value.signed_peer_record = reader.bytes();

   const auto endpoint_count = reader.u64();
   for (auto i = std::uint64_t{}; i < endpoint_count; ++i) {
      auto endpoint = peer_store::endpoint_record{};
      endpoint.endpoint = reader.endpoint();
      endpoint.kind = static_cast<path::kind>(reader.u64());
      endpoint.relay_peer = reader.optional_peer();
      endpoint.successes = reader.u64();
      endpoint.failures = reader.u64();
      endpoint.last_latency = std::chrono::milliseconds{reader.i64()};
      endpoint.backoff_until = reader.time();
      endpoint.score = reader.f64();
      value.endpoints.push_back(std::move(endpoint));
   }

   const auto reservation_count = reader.u64();
   for (auto i = std::uint64_t{}; i < reservation_count; ++i) {
      auto reservation = peer_store::relay_record{};
      reservation.relay = reader.peer();
      reservation.reservation_id = reader.u64();
      reservation.expires_at = reader.time();
      const auto relay_endpoint_count = reader.u64();
      for (auto j = std::uint64_t{}; j < relay_endpoint_count; ++j) {
         reservation.endpoints.push_back(reader.endpoint());
      }
      reservation.voucher = reader.bytes();
      reservation.successes = reader.u64();
      reservation.failures = reader.u64();
      reservation.last_latency = std::chrono::milliseconds{reader.i64()};
      reservation.score = reader.f64();
      value.relay_reservations.push_back(std::move(reservation));
   }

   value.reachability = static_cast<reachability::state>(reader.u64());
   value.observed_endpoint = reader.optional_endpoint();
   value.reachability_expires_at = reader.time();
   value.successes = reader.u64();
   value.failures = reader.u64();
   value.last_latency = std::chrono::milliseconds{reader.i64()};
   value.score = reader.f64();
   reader.finish();
   return value;
}

void mutate_endpoint(peer_store::record& record, const fcl::quic::endpoint& endpoint, path::kind kind,
                     auto&& callback) {
   auto iterator = std::ranges::find_if(record.endpoints, [&](const auto& current) {
      return same_endpoint(current.endpoint, endpoint);
   });
   if (iterator == record.endpoints.end()) {
      iterator = record.endpoints.insert(record.endpoints.end(), peer_store::endpoint_record{.endpoint = endpoint, .kind = kind});
   }
   iterator->kind = kind;
   callback(*iterator);
   refresh_endpoint_score(*iterator);
}

class memory_peer_store_backend final : public peer_store::backend {
 public:
   void upsert(peer_store::record value) override {
      auto lock = std::scoped_lock{mutex_};
      normalize_for_storage(value);
      records_[value.peer] = std::move(value);
   }

   void learn_endpoint(peer_id peer, fcl::quic::endpoint endpoint, capability_set capabilities) override {
      auto lock = std::scoped_lock{mutex_};
      auto& record = records_[peer];
      record.peer = std::move(peer);
      record.capabilities.bits |= capabilities.bits;
      const auto exists = std::ranges::any_of(record.endpoints, [&](const peer_store::endpoint_record& current) {
         return same_endpoint(current.endpoint, endpoint);
      });
      if (!exists) {
         auto entry = peer_store::endpoint_record{.endpoint = std::move(endpoint)};
         refresh_endpoint_score(entry);
         record.endpoints.push_back(std::move(entry));
      }
      normalize_for_storage(record);
   }

   void mark_reachability(peer_id peer, reachability::state state,
                          std::optional<fcl::quic::endpoint> observed) override {
      auto lock = std::scoped_lock{mutex_};
      auto& record = records_[peer];
      record.peer = std::move(peer);
      record.reachability = state;
      record.observed_endpoint = std::move(observed);
      record.reachability_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5};
   }

   void mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) override {
      auto lock = std::scoped_lock{mutex_};
      auto& record = records_[peer];
      record.peer = peer;
      ++record.successes;
      record.last_latency = latency;
      refresh_record_score(record, kind, true);
   }

   void mark_failure(const peer_id& peer) override {
      auto lock = std::scoped_lock{mutex_};
      auto& record = records_[peer];
      record.peer = peer;
      ++record.failures;
      const auto kind = record.endpoints.empty() ? path::kind::direct : record.endpoints.front().kind;
      refresh_record_score(record, kind, false);
   }

   void mark_endpoint_success(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                              std::chrono::milliseconds latency) override {
      auto lock = std::scoped_lock{mutex_};
      auto& record = records_[peer];
      record.peer = peer;
      mutate_endpoint(record, endpoint, kind, [&](peer_store::endpoint_record& current) {
         current.last_latency = latency;
         current.backoff_until = {};
         ++current.successes;
      });
      ++record.successes;
      record.last_latency = latency;
      refresh_record_score(record, kind, true);
   }

   void mark_endpoint_failure(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                              std::chrono::system_clock::time_point backoff_until) override {
      auto lock = std::scoped_lock{mutex_};
      auto& record = records_[peer];
      record.peer = peer;
      mutate_endpoint(record, endpoint, kind, [&](peer_store::endpoint_record& current) {
         current.backoff_until = backoff_until;
         ++current.failures;
      });
      ++record.failures;
      refresh_record_score(record, kind, false);
   }

   std::optional<peer_store::record> find(const peer_id& peer) const override {
      auto lock = std::scoped_lock{mutex_};
      const auto it = records_.find(peer);
      if (it == records_.end()) {
         return std::nullopt;
      }
      auto out = it->second;
      expire_reachability(out, std::chrono::system_clock::now());
      return out;
   }

   std::vector<peer_store::record> snapshot() const override {
      auto lock = std::scoped_lock{mutex_};
      auto out = std::vector<peer_store::record>{};
      out.reserve(records_.size());
      const auto now = std::chrono::system_clock::now();
      for (const auto& [_, record] : records_) {
         auto copy = record;
         expire_reachability(copy, now);
         out.push_back(std::move(copy));
      }
      return out;
   }

 private:
   mutable std::mutex mutex_;
   std::map<peer_id, peer_store::record> records_;
};

class rocksdb_peer_store_backend final : public peer_store::backend {
 public:
   explicit rocksdb_peer_store_backend(peer_store::rocksdb_options options) : options_(std::move(options)) {
      if (options_.path.empty()) {
         exceptions::raise(exceptions::code::invalid_options, "RocksDB peer store path is required");
      }
      if (options_.key_prefix.empty()) {
         exceptions::raise(exceptions::code::invalid_options, "RocksDB peer store key prefix is required");
      }

      auto rocksdb_options = rocksdb::Options{};
      rocksdb_options.create_if_missing = options_.create_if_missing;
      auto db = std::unique_ptr<rocksdb::DB>{};
      const auto status = rocksdb::DB::Open(rocksdb_options, options_.path.string(), &db);
      if (!status.ok()) {
         exceptions::raise(exceptions::code::internal, "failed to open RocksDB peer store: " + status.ToString());
      }
      db_ = std::move(db);
   }

   void upsert(peer_store::record value) override {
      normalize_for_storage(value);
      put(value);
   }

   void learn_endpoint(peer_id peer, fcl::quic::endpoint endpoint, capability_set capabilities) override {
      mutate(std::move(peer), [&](peer_store::record& record) {
         record.capabilities.bits |= capabilities.bits;
         const auto exists = std::ranges::any_of(record.endpoints, [&](const peer_store::endpoint_record& current) {
            return same_endpoint(current.endpoint, endpoint);
         });
         if (!exists) {
            auto entry = peer_store::endpoint_record{.endpoint = std::move(endpoint)};
            refresh_endpoint_score(entry);
            record.endpoints.push_back(std::move(entry));
         }
      });
   }

   void mark_reachability(peer_id peer, reachability::state state,
                          std::optional<fcl::quic::endpoint> observed) override {
      mutate(std::move(peer), [&](peer_store::record& record) {
         record.reachability = state;
         record.observed_endpoint = std::move(observed);
         record.reachability_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5};
      });
   }

   void mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) override {
      mutate(peer, [&](peer_store::record& record) {
         ++record.successes;
         record.last_latency = latency;
         refresh_record_score(record, kind, true);
      });
   }

   void mark_failure(const peer_id& peer) override {
      mutate(peer, [&](peer_store::record& record) {
         ++record.failures;
         const auto kind = record.endpoints.empty() ? path::kind::direct : record.endpoints.front().kind;
         refresh_record_score(record, kind, false);
      });
   }

   void mark_endpoint_success(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                              std::chrono::milliseconds latency) override {
      mutate(peer, [&](peer_store::record& record) {
         mutate_endpoint(record, endpoint, kind, [&](peer_store::endpoint_record& current) {
            current.last_latency = latency;
            current.backoff_until = {};
            ++current.successes;
         });
         ++record.successes;
         record.last_latency = latency;
         refresh_record_score(record, kind, true);
      });
   }

   void mark_endpoint_failure(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                              std::chrono::system_clock::time_point backoff_until) override {
      mutate(peer, [&](peer_store::record& record) {
         mutate_endpoint(record, endpoint, kind, [&](peer_store::endpoint_record& current) {
            current.backoff_until = backoff_until;
            ++current.failures;
         });
         ++record.failures;
         refresh_record_score(record, kind, false);
      });
   }

   std::optional<peer_store::record> find(const peer_id& peer) const override {
      auto lock = std::scoped_lock{mutex_};
      return read_locked(peer, true);
   }

   std::vector<peer_store::record> snapshot() const override {
      auto lock = std::scoped_lock{mutex_};
      auto out = std::vector<peer_store::record>{};
      std::unique_ptr<rocksdb::Iterator> iterator{db_->NewIterator(rocksdb::ReadOptions{})};
      const auto prefix = key_prefix();
      const auto now = std::chrono::system_clock::now();
      for (iterator->Seek(prefix); iterator->Valid() && iterator->key().starts_with(prefix); iterator->Next()) {
         auto record = decode_record(iterator->value().ToString());
         expire_reachability(record, now);
         out.push_back(std::move(record));
      }
      const auto status = iterator->status();
      if (!status.ok()) {
         exceptions::raise(exceptions::code::internal, "failed to scan RocksDB peer store: " + status.ToString());
      }
      return out;
   }

 private:
   [[nodiscard]] std::string key_prefix() const {
      return options_.key_prefix + "/peer/";
   }

   [[nodiscard]] std::string key_for(const peer_id& peer) const {
      return key_prefix() + peer.value;
   }

   [[nodiscard]] std::optional<peer_store::record> read_locked(const peer_id& peer, bool expire_copy) const {
      auto value = std::string{};
      const auto status = db_->Get(rocksdb::ReadOptions{}, key_for(peer), &value);
      if (status.IsNotFound()) {
         return std::nullopt;
      }
      if (!status.ok()) {
         exceptions::raise(exceptions::code::internal, "failed to read RocksDB peer store: " + status.ToString());
      }
      auto record = decode_record(value);
      if (expire_copy) {
         expire_reachability(record, std::chrono::system_clock::now());
      }
      return record;
   }

   void put(const peer_store::record& value) {
      auto lock = std::scoped_lock{mutex_};
      put_locked(value);
   }

   void put_locked(const peer_store::record& value) {
      const auto status = db_->Put(rocksdb::WriteOptions{}, key_for(value.peer), encode_record(value));
      if (!status.ok()) {
         exceptions::raise(exceptions::code::internal, "failed to write RocksDB peer store: " + status.ToString());
      }
   }

   void mutate(peer_id peer, auto&& callback) {
      auto lock = std::scoped_lock{mutex_};
      auto record = read_locked(peer, false).value_or(peer_store::record{.peer = peer});
      record.peer = std::move(peer);
      callback(record);
      normalize_for_storage(record);
      put_locked(record);
   }

   peer_store::rocksdb_options options_;
   mutable std::mutex mutex_;
   std::unique_ptr<rocksdb::DB> db_;
};

} // namespace

struct peer_store::impl {
   std::shared_ptr<backend> backend;
};

std::shared_ptr<peer_store::backend> peer_store::make_memory_backend() {
   return std::make_shared<memory_peer_store_backend>();
}

std::shared_ptr<peer_store::backend> peer_store::make_rocksdb_backend(peer_store::rocksdb_options options) {
   return std::make_shared<rocksdb_peer_store_backend>(std::move(options));
}

peer_store::peer_store() : peer_store{options{.backend = make_memory_backend()}} {}

peer_store::peer_store(options options_value)
    : impl_(std::make_shared<impl>(impl{
          .backend = options_value.backend ? std::move(options_value.backend) : make_memory_backend(),
      })) {}

peer_store::~peer_store() = default;
peer_store::peer_store(peer_store&&) noexcept = default;
peer_store& peer_store::operator=(peer_store&&) noexcept = default;

void peer_store::upsert(record value) {
   impl_->backend->upsert(std::move(value));
}

void peer_store::learn_endpoint(peer_id peer, fcl::quic::endpoint endpoint, capability_set capabilities) {
   impl_->backend->learn_endpoint(std::move(peer), std::move(endpoint), capabilities);
}

void peer_store::mark_reachability(peer_id peer, reachability::state state,
                                   std::optional<fcl::quic::endpoint> observed) {
   impl_->backend->mark_reachability(std::move(peer), state, std::move(observed));
}

void peer_store::mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) {
   impl_->backend->mark_success(peer, kind, latency);
}

void peer_store::mark_failure(const peer_id& peer) {
   impl_->backend->mark_failure(peer);
}

void peer_store::mark_endpoint_success(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                                       std::chrono::milliseconds latency) {
   impl_->backend->mark_endpoint_success(peer, endpoint, kind, latency);
}

void peer_store::mark_endpoint_failure(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                                       std::chrono::system_clock::time_point backoff_until) {
   impl_->backend->mark_endpoint_failure(peer, endpoint, kind, backoff_until);
}

std::optional<peer_store::record> peer_store::find(const peer_id& peer) const {
   return impl_->backend->find(peer);
}

std::vector<peer_store::record> peer_store::snapshot() const {
   return impl_->backend->snapshot();
}

} // namespace fcl::p2p
