module;

#include <fcl/exception/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.transport.listener;

import fcl.transport.exceptions;

namespace fcl::transport {

struct stream_listener::impl {
   std::shared_ptr<detail::stream_listener_concept> model;
};

struct session_listener::impl {
   std::shared_ptr<detail::session_listener_concept> model;
};

stream_listener::stream_listener() = default;
stream_listener::stream_listener(std::shared_ptr<detail::stream_listener_concept> model)
    : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

stream_listener::~stream_listener() = default;
stream_listener::stream_listener(stream_listener&&) noexcept = default;
stream_listener& stream_listener::operator=(stream_listener&&) noexcept = default;

bool stream_listener::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

endpoint stream_listener::local_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream listener");
   }
   return impl_->model->local_endpoint();
}

boost::asio::awaitable<connected_stream> stream_listener::async_accept() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream listener");
   }
   co_return co_await impl_->model->async_accept();
}

boost::asio::awaitable<void> stream_listener::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->model->async_close();
}

void stream_listener::cancel() {
   if (valid()) {
      impl_->model->cancel();
   }
}

session_listener::session_listener() = default;
session_listener::session_listener(std::shared_ptr<detail::session_listener_concept> model)
    : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

session_listener::~session_listener() = default;
session_listener::session_listener(session_listener&&) noexcept = default;
session_listener& session_listener::operator=(session_listener&&) noexcept = default;

bool session_listener::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

endpoint session_listener::local_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport session listener");
   }
   return impl_->model->local_endpoint();
}

boost::asio::awaitable<connected_session> session_listener::async_accept() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport session listener");
   }
   co_return co_await impl_->model->async_accept();
}

boost::asio::awaitable<void> session_listener::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->model->async_close();
}

void session_listener::cancel() {
   if (valid()) {
      impl_->model->cancel();
   }
}

stream_listener detail::stream_listener_access::make(std::shared_ptr<stream_listener_concept> model) {
   return stream_listener{std::move(model)};
}

session_listener detail::session_listener_access::make(std::shared_ptr<session_listener_concept> model) {
   return session_listener{std::move(model)};
}

} // namespace fcl::transport
