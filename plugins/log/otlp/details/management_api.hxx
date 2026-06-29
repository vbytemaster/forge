#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>

namespace forge::plugins::log::otlp {

class plugin::management_api final : public api {
 public:
   explicit management_api(std::shared_ptr<impl> state);

   boost::asio::awaitable<flush_result> flush(flush_request value) override;
   boost::asio::awaitable<::forge::plugins::log::otlp::metrics> metrics(metrics_request value) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace forge::plugins::log::otlp
