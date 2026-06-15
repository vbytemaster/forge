module;

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/asio/awaitable.hpp>

module fcl.http.upload;

import fcl.http.exceptions;

namespace fcl::http {

struct upload_spool::state {
   explicit state(std::filesystem::path path_value) : path(std::move(path_value)) {}

   ~state() {
      if (cleanup) {
         std::error_code ignored;
         std::filesystem::remove(path, ignored);
      }
   }

   std::filesystem::path path;
   std::uint64_t size = 0;
   bool cleanup = true;
};

namespace {

std::filesystem::path effective_spool_directory(const upload_options& options) {
   auto directory = options.spool_directory.empty() ? std::filesystem::temp_directory_path()
                                                    : options.spool_directory;
   std::filesystem::create_directories(directory);
   if (!std::filesystem::is_directory(directory)) {
      throw exceptions::bad_request{"upload spool directory is not a directory"};
   }
   return directory;
}

std::string text_from_bytes(const std::vector<std::byte>& bytes) {
   return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<std::byte> bytes_from_text(std::string_view text) {
   auto bytes = std::vector<std::byte>(text.size());
   std::memcpy(bytes.data(), text.data(), text.size());
   return bytes;
}

std::string read_file_text(const std::filesystem::path& path) {
   auto input = std::ifstream{path, std::ios::binary};
   if (!input) {
      throw exceptions::internal{"failed to read upload spool"};
   }
   auto output = std::ostringstream{};
   output << input.rdbuf();
   return output.str();
}

void write_all(int fd, const std::byte* data, std::size_t size) {
   auto written = std::size_t{0};
   while (written != size) {
      const auto result = ::write(fd, data + written, size - written);
      if (result < 0) {
         if (errno == EINTR) {
            continue;
         }
         throw exceptions::internal{"failed to write upload spool"};
      }
      written += static_cast<std::size_t>(result);
   }
}

std::string trim_copy(std::string_view value) {
   while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.remove_prefix(1);
   }
   while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
      value.remove_suffix(1);
   }
   return std::string{value};
}

std::string lower_copy(std::string_view value) {
   auto output = std::string{value};
   std::transform(output.begin(), output.end(), output.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
   });
   return output;
}

std::string unquote(std::string value) {
   if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value.erase(value.begin());
      value.pop_back();
   }
   return value;
}

std::vector<std::string_view> split(std::string_view value, char delimiter) {
   auto parts = std::vector<std::string_view>{};
   auto start = std::size_t{0};
   while (start <= value.size()) {
      const auto found = value.find(delimiter, start);
      const auto end = found == std::string_view::npos ? value.size() : found;
      parts.push_back(value.substr(start, end - start));
      if (found == std::string_view::npos) {
         break;
      }
      start = found + 1U;
   }
   return parts;
}

std::optional<std::string> header_value(const std::vector<std::pair<std::string, std::string>>& headers,
                                        std::string_view name) {
   const auto needle = lower_copy(name);
   for (const auto& [key, value] : headers) {
      if (lower_copy(key) == needle) {
         return value;
      }
   }
   return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> parse_headers(std::string_view value) {
   auto headers = std::vector<std::pair<std::string, std::string>>{};
   auto start = std::size_t{0};
   while (start < value.size()) {
      const auto end = value.find("\r\n", start);
      auto line = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
      const auto colon = line.find(':');
      if (colon == std::string_view::npos) {
         throw exceptions::bad_request{"malformed multipart header"};
      }
      headers.emplace_back(trim_copy(line.substr(0, colon)), trim_copy(line.substr(colon + 1U)));
      if (end == std::string_view::npos) {
         break;
      }
      start = end + 2U;
   }
   return headers;
}

struct disposition {
   std::string name;
   std::optional<std::string> filename;
};

disposition parse_content_disposition(const std::vector<std::pair<std::string, std::string>>& headers) {
   const auto value = header_value(headers, "content-disposition");
   if (!value.has_value()) {
      throw exceptions::bad_request{"multipart part is missing Content-Disposition"};
   }

   auto result = disposition{};
   auto parts = split(*value, ';');
   if (parts.empty() || lower_copy(trim_copy(parts.front())) != "form-data") {
      throw exceptions::bad_request{"multipart part is not form-data"};
   }

   for (auto index = std::size_t{1}; index < parts.size(); ++index) {
      const auto parameter = trim_copy(parts[index]);
      const auto equals = parameter.find('=');
      if (equals == std::string::npos) {
         continue;
      }
      const auto key = lower_copy(trim_copy(std::string_view{parameter}.substr(0, equals)));
      auto parameter_value = unquote(trim_copy(std::string_view{parameter}.substr(equals + 1U)));
      if (key == "name") {
         result.name = std::move(parameter_value);
      } else if (key == "filename") {
         result.filename = std::move(parameter_value);
      }
   }

   if (result.name.empty()) {
      throw exceptions::bad_request{"multipart part is missing name"};
   }
   return result;
}

} // namespace

class upload_spool_writer {
 public:
   explicit upload_spool_writer(const upload_options& options) {
      const auto directory = effective_spool_directory(options);
      const auto prefix = options.spool_prefix.empty() ? std::string{"fcl-http-upload-"} : options.spool_prefix;
      const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
      for (auto attempt = 0; attempt != 64; ++attempt) {
         auto candidate = directory / (prefix + std::to_string(::getpid()) + "-" + std::to_string(seed) + "-" +
                                      std::to_string(attempt) + ".tmp");
         const auto fd = ::open(candidate.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
         if (fd >= 0) {
            fd_ = fd;
            state_ = std::make_shared<upload_spool::state>(std::move(candidate));
            return;
         }
         if (errno != EEXIST && errno != EINTR) {
            throw exceptions::internal{"failed to create upload spool"};
         }
      }
      throw exceptions::internal{"failed to allocate upload spool"};
   }

   ~upload_spool_writer() {
      if (fd_ >= 0) {
         ::close(fd_);
      }
   }

   upload_spool_writer(const upload_spool_writer&) = delete;
   upload_spool_writer& operator=(const upload_spool_writer&) = delete;

   void write(const std::vector<std::byte>& bytes) {
      write(bytes.data(), bytes.size());
   }

   void write(const std::byte* data, std::size_t size) {
      if (size == 0) {
         return;
      }
      write_all(fd_, data, size);
      state_->size += size;
   }

   upload_spool finish() {
      if (fd_ >= 0) {
         ::close(fd_);
         fd_ = -1;
      }
      return upload_spool{state_};
   }

 private:
   int fd_ = -1;
   std::shared_ptr<upload_spool::state> state_;
};

namespace {

upload_part store_part(std::string name, std::optional<std::string> filename, std::string content_type,
                       std::vector<std::pair<std::string, std::string>> headers, std::string_view bytes,
                       const upload_options& options) {
   if (filename.has_value() && bytes.size() > options.max_file_bytes) {
      throw exceptions::payload_too_large{"multipart file exceeds upload limit"};
   }

   auto part = upload_part{
      .name = std::move(name),
      .filename = std::move(filename),
      .content_type = std::move(content_type),
      .headers = std::move(headers),
      .memory = {},
      .spool = std::nullopt,
      .size = static_cast<std::uint64_t>(bytes.size()),
   };

   if (bytes.size() <= options.memory_threshold_bytes) {
      part.memory = bytes_from_text(bytes);
      return part;
   }

   auto writer = upload_spool_writer{options};
   const auto part_bytes = bytes_from_text(bytes);
   writer.write(part_bytes);
   part.spool = writer.finish();
   return part;
}

multipart_form parse_multipart_body(std::string_view body, std::string_view boundary, const upload_options& options) {
   const auto delimiter = std::string{"--"} + std::string{boundary};
   auto position = std::size_t{0};
   if (!body.starts_with(delimiter)) {
      throw exceptions::bad_request{"multipart body does not start with boundary"};
   }
   position += delimiter.size();

   auto form = multipart_form{};
   while (true) {
      if (body.substr(position, 2) == "--") {
         position += 2U;
         if (body.substr(position, 2) == "\r\n") {
            position += 2U;
         }
         if (position != body.size()) {
            throw exceptions::bad_request{"unexpected multipart trailer"};
         }
         return form;
      }
      if (body.substr(position, 2) != "\r\n") {
         throw exceptions::bad_request{"malformed multipart boundary"};
      }
      position += 2U;

      const auto header_end = body.find("\r\n\r\n", position);
      if (header_end == std::string_view::npos) {
         throw exceptions::bad_request{"multipart part headers are incomplete"};
      }
      auto headers = parse_headers(body.substr(position, header_end - position));
      const auto meta = parse_content_disposition(headers);
      auto content_type = header_value(headers, "content-type").value_or("application/octet-stream");
      const auto content_start = header_end + 4U;
      const auto next_boundary = body.find(std::string{"\r\n"} + delimiter, content_start);
      if (next_boundary == std::string_view::npos) {
         throw exceptions::bad_request{"multipart closing boundary is missing"};
      }
      auto content = body.substr(content_start, next_boundary - content_start);
      auto part = store_part(meta.name, meta.filename, std::move(content_type), std::move(headers), content, options);
      if (part.filename.has_value()) {
         form.files.push_back(part);
      }
      form.parts.push_back(std::move(part));
      position = next_boundary + 2U + delimiter.size();
   }
}

} // namespace

upload_spool::upload_spool(std::shared_ptr<state> state_value) : state_(std::move(state_value)) {}

bool upload_spool::valid() const noexcept {
   return static_cast<bool>(state_);
}

const std::filesystem::path& upload_spool::path() const {
   if (!state_) {
      throw exceptions::internal{"upload spool is not valid"};
   }
   return state_->path;
}

std::uint64_t upload_spool::size() const noexcept {
   return state_ ? state_->size : 0;
}

void upload_spool::release() noexcept {
   if (state_) {
      state_->cleanup = false;
   }
}

bool upload_part::in_memory() const noexcept {
   return !spool.has_value();
}

std::string upload_part::text() const {
   if (spool.has_value()) {
      return read_file_text(spool->path());
   }
   return text_from_bytes(memory);
}

std::optional<std::string> multipart_form::field(std::string_view name) const {
   for (const auto& part : parts) {
      if (!part.filename.has_value() && part.name == name) {
         return part.text();
      }
   }
   return std::nullopt;
}

upload_reader::upload_reader(body_reader body, upload_options options)
    : body_(std::move(body)), options_(std::move(options)) {}

boost::asio::awaitable<upload_part> upload_reader::async_read() {
   auto memory = std::vector<std::byte>{};
   auto total = std::uint64_t{0};
   auto writer = std::optional<upload_spool_writer>{};

   while (auto chunk = co_await body_.async_read()) {
      total += chunk->bytes.size();
      if (total > options_.max_total_bytes || total > options_.max_file_bytes) {
         throw exceptions::payload_too_large{"upload exceeds configured limit"};
      }

      if (!writer.has_value() && memory.size() + chunk->bytes.size() <= options_.memory_threshold_bytes) {
         memory.insert(memory.end(), chunk->bytes.begin(), chunk->bytes.end());
         continue;
      }

      if (!writer.has_value()) {
         writer.emplace(options_);
         writer->write(memory);
         memory.clear();
      }
      writer->write(chunk->bytes);
   }

   if (writer.has_value()) {
      co_return upload_part{.memory = {}, .spool = writer->finish(), .size = total};
   }

   co_return upload_part{.memory = std::move(memory), .spool = std::nullopt, .size = total};
}

boost::asio::awaitable<multipart_form> upload_reader::async_read_multipart(std::string_view content_type) {
   const auto boundary = multipart_boundary(content_type);
   if (!boundary.has_value()) {
      throw exceptions::bad_request{"multipart boundary is missing"};
   }

   auto part = co_await async_read();
   co_return parse_multipart_body(part.text(), *boundary, options_);
}

std::optional<std::string> multipart_boundary(std::string_view content_type) {
   for (auto piece : split(content_type, ';')) {
      auto parameter = trim_copy(piece);
      const auto equals = parameter.find('=');
      if (equals == std::string::npos) {
         continue;
      }
      const auto key = lower_copy(trim_copy(std::string_view{parameter}.substr(0, equals)));
      if (key != "boundary") {
         continue;
      }
      auto value = unquote(trim_copy(std::string_view{parameter}.substr(equals + 1U)));
      if (value.empty() || value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
         throw exceptions::bad_request{"multipart boundary is invalid"};
      }
      return value;
   }
   return std::nullopt;
}

} // namespace fcl::http
