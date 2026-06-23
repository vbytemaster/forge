module;

#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <memory>
#include <string>

export module forge.asio.runtime;

export namespace forge::asio {

struct runtime_options {
   std::size_t worker_threads = 1;
   std::string thread_name = "forge-asio";
};

class runtime {
 public:
   explicit runtime(runtime_options options = {});
   ~runtime();

   runtime(const runtime&) = delete;
   runtime& operator=(const runtime&) = delete;

   runtime(runtime&&) = delete;
   runtime& operator=(runtime&&) = delete;

   [[nodiscard]] boost::asio::io_context& context();
   [[nodiscard]] const boost::asio::io_context& context() const;
   void stop();

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::asio
