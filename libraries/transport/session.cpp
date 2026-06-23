module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.transport.session;

import forge.transport.exceptions;

namespace forge::transport {

struct session::impl {
   std::shared_ptr<detail::session_concept> model;
};

session::session() = default;
session::session(std::shared_ptr<detail::session_concept> model) : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

session::~session() = default;
session::session(session&&) noexcept = default;
session& session::operator=(session&&) noexcept = default;

bool session::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

boost::asio::awaitable<stream> session::async_open_stream() {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport session");
   }
   co_return co_await impl_->model->async_open_stream();
}

boost::asio::awaitable<stream> session::async_accept_stream() {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport session");
   }
   co_return co_await impl_->model->async_accept_stream();
}

boost::asio::awaitable<void> session::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->model->async_close();
}

void session::cancel() {
   if (valid()) {
      impl_->model->cancel();
   }
}

session detail::session_access::make(std::shared_ptr<session_concept> model) {
   return session{std::move(model)};
}

} // namespace forge::transport
