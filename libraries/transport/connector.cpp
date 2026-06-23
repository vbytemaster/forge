module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.transport.connector;

import forge.transport.exceptions;

namespace forge::transport {

struct stream_connector::impl {
   std::shared_ptr<detail::stream_connector_concept> model;
};

struct session_connector::impl {
   std::shared_ptr<detail::session_connector_concept> model;
};

stream_connector::stream_connector() = default;
stream_connector::stream_connector(std::shared_ptr<detail::stream_connector_concept> model)
    : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

stream_connector::~stream_connector() = default;
stream_connector::stream_connector(stream_connector&&) noexcept = default;
stream_connector& stream_connector::operator=(stream_connector&&) noexcept = default;

bool stream_connector::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

boost::asio::awaitable<stream_connection> stream_connector::async_connect(endpoint remote, connect_options options) {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream connector");
   }
   co_return co_await impl_->model->async_connect(std::move(remote), options);
}

void stream_connector::cancel() {
   if (valid()) {
      impl_->model->cancel();
   }
}

session_connector::session_connector() = default;
session_connector::session_connector(std::shared_ptr<detail::session_connector_concept> model)
    : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

session_connector::~session_connector() = default;
session_connector::session_connector(session_connector&&) noexcept = default;
session_connector& session_connector::operator=(session_connector&&) noexcept = default;

bool session_connector::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

boost::asio::awaitable<session_connection> session_connector::async_connect(endpoint remote, connect_options options) {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport session connector");
   }
   co_return co_await impl_->model->async_connect(std::move(remote), options);
}

void session_connector::cancel() {
   if (valid()) {
      impl_->model->cancel();
   }
}

stream_connector detail::stream_connector_access::make(std::shared_ptr<stream_connector_concept> model) {
   return stream_connector{std::move(model)};
}

session_connector detail::session_connector_access::make(std::shared_ptr<session_connector_concept> model) {
   return session_connector{std::move(model)};
}

} // namespace forge::transport
