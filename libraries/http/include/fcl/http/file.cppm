module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>

export module fcl.http.file;

import fcl.http.body;
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
   file_response() = default;

   [[nodiscard]] static file_response from_path(std::filesystem::path path, file_options options = {}) {
      auto result = file_response{};
      result.path_ = std::move(path);
      result.options_ = std::move(options);
      result.server_path_ = true;
      return result;
   }

   [[nodiscard]] static file_response from_body(response head, body_reader body) {
      auto result = file_response{};
      result.head_ = std::move(head);
      result.body_ = std::move(body);
      result.server_path_ = false;
      return result;
   }

   [[nodiscard]] status status_code() const noexcept {
      return head_.result();
   }

   [[nodiscard]] const response& head() const noexcept {
      return head_;
   }

   [[nodiscard]] std::string content_type() const {
      if (auto found = head_.find(field::content_type); found != head_.end()) {
         return std::string{found->value()};
      }
      return options_.content_type;
   }

   [[nodiscard]] body_reader& body() noexcept {
      return body_;
   }

   boost::asio::awaitable<void> save_to(const std::filesystem::path& target);

   [[nodiscard]] boost::asio::awaitable<stream_response> materialize(const request& request_value) &&;

 private:
   std::filesystem::path path_;
   file_options options_;
   response head_{status::ok, 11};
   body_reader body_;
   bool server_path_ = false;
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
