#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/asio/awaitable.hpp>

namespace forge::net::p2p {

boost::asio::awaitable<std::vector<std::uint8_t>> async_read_length_delimited(forge::net::p2p::stream& stream,
                                                                              std::vector<std::uint8_t>& buffer,
                                                                              std::size_t max_payload_size);

} // namespace forge::net::p2p
