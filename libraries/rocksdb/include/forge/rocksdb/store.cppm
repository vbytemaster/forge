module;

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

export module forge.rocksdb.store;

export import forge.rocksdb.types;

export namespace forge::rocksdb {

class snapshot {
 public:
   ~snapshot();

   snapshot(const snapshot&) = delete;
   snapshot& operator=(const snapshot&) = delete;

   snapshot(snapshot&&) noexcept;
   snapshot& operator=(snapshot&&) noexcept;

   [[nodiscard]] std::optional<std::vector<std::byte>>
   get(family column_family, std::vector<std::byte> key, read_options options = {});

   [[nodiscard]] std::vector<entry> scan(family column_family, std::vector<std::byte> prefix, read_options options = {});
   [[nodiscard]] scan_result scan_page(family column_family, scan_request request);

 private:
   friend class store;

   struct impl;

   explicit snapshot(std::unique_ptr<impl> impl_value);
   void ensure_active(std::string_view context) const;

   std::unique_ptr<impl> impl_;
};

class transaction {
 public:
   ~transaction();

   transaction(const transaction&) = delete;
   transaction& operator=(const transaction&) = delete;

   transaction(transaction&&) noexcept;
   transaction& operator=(transaction&&) noexcept;

   [[nodiscard]] std::optional<std::vector<std::byte>>
   get(family column_family, std::vector<std::byte> key, read_options options = {});

   [[nodiscard]] std::vector<entry> scan(family column_family, std::vector<std::byte> prefix, read_options options = {});
   [[nodiscard]] scan_result scan_page(family column_family, scan_request request);

   void lock(family column_family, std::vector<std::byte> key, read_options options = {});
   void put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value);
   void erase(family column_family, std::vector<std::byte> key);
   void commit();
   void rollback();

 private:
   friend class store;

   struct impl;

   explicit transaction(std::unique_ptr<impl> impl_value);
   void ensure_active(std::string_view context) const;
   void rollback_if_active() noexcept;

   std::unique_ptr<impl> impl_;
};

class store {
 public:
   struct impl;

   explicit store(config value);
   ~store();

   store(const store&) = delete;
   store& operator=(const store&) = delete;

   store(store&&) noexcept;
   store& operator=(store&&) noexcept;

   [[nodiscard]] std::optional<std::vector<std::byte>>
   get(family column_family, std::vector<std::byte> key, read_options options = {});

   void put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value, write_options options = {});
   void erase(family column_family, std::vector<std::byte> key, write_options options = {});
   void write(std::vector<operation> operations, write_options options = {});

   [[nodiscard]] std::vector<entry> scan(family column_family, std::vector<std::byte> prefix, read_options options = {});
   [[nodiscard]] scan_result scan_page(family column_family, scan_request request);
   [[nodiscard]] transaction begin(write_options options = {});
   [[nodiscard]] snapshot begin_snapshot();

   void flush_wal(bool sync = true);

 private:
   std::shared_ptr<impl> impl_;
};

} // namespace forge::rocksdb
