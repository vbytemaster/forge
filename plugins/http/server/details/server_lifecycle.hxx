#pragma once

namespace forge::plugins::http::server {

boost::asio::awaitable<void> start_server(plugin::impl& state);
boost::asio::awaitable<void> stop_server(plugin::impl& state);
void request_server_stop(plugin::impl& state) noexcept;

} // namespace forge::plugins::http::server
