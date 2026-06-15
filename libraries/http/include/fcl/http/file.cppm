module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

export module fcl.http.file;

import fcl.http.stream;
import fcl.http.types;

export namespace fcl::http {

enum class symlink_policy {
   reject,
   follow,
};

struct file_options {
   std::string content_type = "application/octet-stream";
   std::size_t chunk_bytes = 64 * 1024;
   symlink_policy symlinks = symlink_policy::reject;
   bool etag = true;
   bool last_modified = true;
};

struct file_response {
   std::filesystem::path path;
   file_options options;

   [[nodiscard]] boost::asio::awaitable<stream_response> to_stream_response(const request& request_value) const;
};

class static_file_root {
 public:
   explicit static_file_root(std::filesystem::path root, file_options options = {});

   [[nodiscard]] const std::filesystem::path& root() const noexcept;
   [[nodiscard]] boost::asio::awaitable<stream_response> serve(stream_request& request_value,
                                                               std::string_view relative_path) const;

 private:
   std::filesystem::path root_;
   file_options options_;
};

} // namespace fcl::http
