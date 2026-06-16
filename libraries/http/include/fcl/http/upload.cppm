module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.http.upload;

import fcl.http.body;

export namespace fcl::http {

struct upload_options {
   std::uint64_t memory_threshold_bytes = 1 * 1024 * 1024;
   std::uint64_t max_file_bytes = 64 * 1024 * 1024;
   std::uint64_t max_field_bytes = 1 * 1024 * 1024;
   std::uint64_t max_total_bytes = 128 * 1024 * 1024;
   std::filesystem::path spool_directory;
   std::string spool_prefix = "fcl-http-upload-";
};

class upload_spool {
 public:
   upload_spool() = default;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const std::filesystem::path& path() const;
   [[nodiscard]] std::uint64_t size() const noexcept;

   void release() noexcept;

 private:
   struct state;

   explicit upload_spool(std::shared_ptr<state> state_value);

   std::shared_ptr<state> state_;

   friend class upload_spool_writer;
   friend class upload_reader;
};

struct upload_part {
   std::string name;
   std::optional<std::string> filename;
   std::string content_type;
   std::vector<std::pair<std::string, std::string>> headers;
   std::vector<std::byte> memory;
   std::optional<upload_spool> spool;
   std::uint64_t size = 0;

   [[nodiscard]] bool in_memory() const noexcept;
   [[nodiscard]] std::string text() const;
   [[nodiscard]] std::optional<std::string> safe_filename() const;
};

struct multipart_form {
   std::vector<upload_part> parts;
   std::vector<upload_part> files;

   [[nodiscard]] std::optional<std::string> field(std::string_view name) const;
};

class upload_reader {
 public:
   explicit upload_reader(body_reader body, upload_options options = {});

   [[nodiscard]] boost::asio::awaitable<upload_part> async_read();
   [[nodiscard]] boost::asio::awaitable<multipart_form> async_read_multipart(std::string_view content_type);

 private:
   body_reader body_;
   upload_options options_;
};

[[nodiscard]] std::optional<std::string> multipart_boundary(std::string_view content_type);
[[nodiscard]] std::optional<std::string> sanitize_upload_filename(std::string_view filename);

} // namespace fcl::http
