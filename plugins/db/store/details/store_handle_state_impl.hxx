#pragma once

namespace forge::plugins::db::store {

class plugin::store_handle_state_impl final : public store_handle_state {
 public:
   store_handle_state_impl(std::weak_ptr<impl> owner, std::string name);

   [[nodiscard]] std::string name() const override;
   [[nodiscard]] std::shared_ptr<forge::db::object::store> require_objects() const override;
   [[nodiscard]] std::shared_ptr<forge::db::blob::store> require_blobs() const override;
   [[nodiscard]] std::shared_ptr<forge::db::revision::store> require_revisions() const override;
   boost::asio::awaitable<transaction> begin_transaction() const override;

 private:
   [[nodiscard]] std::shared_ptr<forge::db::core::driver> require_driver() const override;

   std::weak_ptr<impl> owner_;
   std::string name_;
};

} // namespace forge::plugins::db::store
