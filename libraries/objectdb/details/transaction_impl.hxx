#pragma once

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace forge::objectdb {

struct transaction::impl {
   impl(std::unique_ptr<session> active_value,
        transaction::ensure_registered_fn ensure,
        std::vector<std::shared_ptr<interceptor>> interceptors_value,
        std::vector<std::shared_ptr<observer>> observers_value,
        transaction::release_fn release,
        boost::asio::any_io_executor executor) noexcept;

   ~impl();

   std::unique_ptr<session> active;
   transaction::ensure_registered_fn ensure_registered;
   std::vector<std::shared_ptr<interceptor>> interceptors;
   std::vector<std::shared_ptr<observer>> observers;
   transaction::release_fn release_writer;
   std::optional<boost::asio::any_io_executor> cleanup_executor;
   change_set changes;
   bool closed = false;
   bool committed = false;

   void release() noexcept;
   void rollback_on_drop() noexcept;
};

} // namespace forge::objectdb
