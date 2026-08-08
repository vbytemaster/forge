#pragma once

#include <boost/asio/awaitable.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace forge::db::authenticated::detail {

class transaction_participant_impl final : public forge::db::core::transaction_participant {
 public:
   transaction_participant_impl(forge::db::core::family family, std::string domain, digest namespace_hash);

   [[nodiscard]] std::string_view name() const noexcept override;
   [[nodiscard]] forge::db::core::mutation_policy classify(const forge::db::core::family& family,
                                                           const forge::db::core::record_key& key,
                                                           forge::db::core::mutation_kind kind) const noexcept override;
   [[nodiscard]] std::optional<forge::db::core::record_address>
   make_retention_guard(const forge::db::core::record_mutation& mutation,
                        std::span<const std::byte> token) const override;
   boost::asio::awaitable<void> prepare_savepoint(forge::db::core::savepoint_id_t id) override;
   void publish_savepoint(forge::db::core::savepoint_id_t id) noexcept override;
   void discard_savepoint(forge::db::core::savepoint_id_t id) noexcept override;
   boost::asio::awaitable<void> rollback_to_savepoint(forge::db::core::savepoint_id_t id,
                                                      forge::db::core::participant_access& access) override;
   boost::asio::awaitable<void> release_savepoint(forge::db::core::savepoint_id_t id,
                                                  forge::db::core::participant_access& access) override;
   boost::asio::awaitable<void> prepare_commit(forge::db::core::participant_access& access) override;

   void set_staged(staged_version value);
   [[nodiscard]] std::optional<staged_version> staged() const;

 private:
   struct savepoint_frame {
      forge::db::core::savepoint_id_t id = 0;
      std::optional<staged_version> staged;
   };

   std::string name_;
   forge::db::core::family family_;
   digest namespace_hash_;
   std::optional<staged_version> staged_;
   std::optional<savepoint_frame> pending_savepoint_;
   std::vector<savepoint_frame> savepoints_;
};

} // namespace forge::db::authenticated::detail
