module;

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.http.file;

import fcl.http.body;
import fcl.http.exceptions;
import fcl.http.range;
import fcl.http.types;

namespace fcl::http {
namespace {

std::string http_date(std::filesystem::file_time_type value) {
   const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      value - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
   const auto time = std::chrono::system_clock::to_time_t(system_time);
   auto tm = std::tm{};
#if defined(_WIN32)
   gmtime_s(&tm, &time);
#else
   gmtime_r(&time, &tm);
#endif
   auto output = std::ostringstream{};
   output << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
   return output.str();
}

std::string file_etag(std::uintmax_t size, std::filesystem::file_time_type modified) {
   const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
   auto output = std::ostringstream{};
   output << "W/\"" << size << '-' << ticks << '"';
   return output.str();
}

std::optional<std::string_view> header_value(const request& request_value, field name) {
   const auto found = request_value.find(name);
   if (found == request_value.end()) {
      return std::nullopt;
   }
   return std::string_view{found->value().data(), found->value().size()};
}

bool same_header_value(std::optional<std::string_view> left, std::string_view right) {
   return left.has_value() && *left == right;
}

std::vector<std::string> split_relative_path(std::string_view value) {
   if (value.find('\\') != std::string_view::npos) {
      throw exceptions::forbidden{"unsafe static file path"};
   }

   auto segments = std::vector<std::string>{};
   auto start = std::size_t{0};
   while (start <= value.size()) {
      const auto separator = value.find('/', start);
      const auto end = separator == std::string_view::npos ? value.size() : separator;
      auto segment = value.substr(start, end - start);
      if (segment.empty() || segment == "." || segment == "..") {
         throw exceptions::forbidden{"unsafe static file path"};
      }
      segments.emplace_back(segment);
      if (separator == std::string_view::npos) {
         break;
      }
      start = separator + 1U;
   }
   return segments;
}

std::filesystem::path resolve_child(const std::filesystem::path& root, std::string_view relative_path,
                                    symlink_policy symlinks) {
   const auto relative = std::filesystem::path{relative_path};
   if (relative_path.empty() || relative_path.front() == '/' || relative.is_absolute()) {
      throw exceptions::forbidden{"unsafe static file path"};
   }

   auto current = root;
   for (const auto& segment : split_relative_path(relative_path)) {
      current /= segment;
      if (symlinks == symlink_policy::reject && std::filesystem::is_symlink(std::filesystem::symlink_status(current))) {
         throw exceptions::forbidden{"static file symlink is not allowed"};
      }
   }
   return current;
}

bool path_contains(const std::filesystem::path& root, const std::filesystem::path& child) {
   auto root_it = root.begin();
   auto child_it = child.begin();
   for (; root_it != root.end(); ++root_it, ++child_it) {
      if (child_it == child.end() || *root_it != *child_it) {
         return false;
      }
   }
   return true;
}

std::filesystem::path resolve_contained_child(const std::filesystem::path& root, std::string_view relative_path,
                                              symlink_policy symlinks) {
   const auto candidate = resolve_child(root, relative_path, symlinks);
   const auto canonical = std::filesystem::weakly_canonical(candidate);
   if (!path_contains(root, canonical)) {
      throw exceptions::forbidden{"static file path escapes root"};
   }
   return canonical;
}

stream_response not_modified_response(const request& request_value, const std::string& etag_value,
                                      const std::string& modified_value) {
   auto reply = response{status::not_modified, request_value.version()};
   reply.set(field::etag, etag_value);
   reply.set(field::last_modified, modified_value);
   reply.keep_alive(request_value.keep_alive());
   return stream_response::buffered(std::move(reply));
}

std::shared_ptr<std::ifstream> open_file_at(const std::filesystem::path& path, std::uint64_t offset) {
   auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
   if (!*file) {
      throw exceptions::not_found{"file not found"};
   }
   file->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
   return file;
}

stream_response make_file_stream(const request& request_value, const std::filesystem::path& path,
                                 const file_options& options, range_response range_value) {
   auto reply = response{range_value.partial ? status::partial_content : status::ok, request_value.version()};
   reply.set(field::content_type, options.content_type);
   reply.set(field::accept_ranges, "bytes");
   reply.keep_alive(request_value.keep_alive());

   const auto file_size = std::filesystem::file_size(path);
   const auto modified = std::filesystem::last_write_time(path);
   const auto modified_value = http_date(modified);
   const auto etag_value = file_etag(file_size, modified);
   if (options.etag) {
      reply.set(field::etag, etag_value);
   }
   if (options.last_modified) {
      reply.set(field::last_modified, modified_value);
   }

   if (options.etag && same_header_value(header_value(request_value, field::if_none_match), etag_value)) {
      return not_modified_response(request_value, etag_value, modified_value);
   }
   if (options.last_modified &&
       same_header_value(header_value(request_value, field::if_modified_since), modified_value)) {
      return not_modified_response(request_value, etag_value, modified_value);
   }

   if (!range_value.satisfiable) {
      reply.result(status::range_not_satisfiable);
      reply.set(field::content_range, range_value.content_range);
      reply.set(field::content_length, "0");
      return stream_response::buffered(std::move(reply));
   }

   const auto first = range_value.bytes.first;
   const auto last = range_value.bytes.last;
   const auto length = file_size == 0 ? std::uint64_t{0} : last - first + 1U;
   if (range_value.partial) {
      reply.set(field::content_range, range_value.content_range);
   }
   reply.set(field::content_length, std::to_string(length));

   if (request_value.method() == method::head) {
      return stream_response::buffered(std::move(reply));
   }

   auto file = open_file_at(path, first);
   auto remaining = std::make_shared<std::uint64_t>(length);
   const auto chunk_size = std::max<std::size_t>(1, options.chunk_bytes);
   return stream_response{
       .head = std::move(reply),
       .body =
          [file, remaining, chunk_size]() mutable -> boost::asio::awaitable<std::optional<body_chunk>> {
             if (*remaining == 0) {
                co_return std::nullopt;
             }
             const auto bytes_to_read =
                static_cast<std::size_t>(std::min<std::uint64_t>(*remaining, static_cast<std::uint64_t>(chunk_size)));
             auto bytes = std::vector<std::byte>(bytes_to_read);
             file->read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
             const auto read = static_cast<std::size_t>(file->gcount());
             if (read == 0) {
                co_return std::nullopt;
             }
             bytes.resize(read);
             *remaining -= read;
             co_return body_chunk{.bytes = std::move(bytes)};
          },
   };
}

} // namespace

boost::asio::awaitable<stream_response> file_response::materialize(const request& request_value) const {
   if (!server_path_) {
      co_return stream_response::buffered(head_);
   }
   if (!std::filesystem::is_regular_file(path_)) {
      co_return stream_response::buffered(make_text_response(request_value, status::not_found, "not found"));
   }

   const auto size = static_cast<std::uint64_t>(std::filesystem::file_size(path_));
   auto range_value = resolve_range(header_value(request_value, field::range), size);
   co_return make_file_stream(request_value, path_, options_, std::move(range_value));
}

boost::asio::awaitable<void> file_response::save_to(const std::filesystem::path& target) {
   auto output = std::ofstream{target, std::ios::binary | std::ios::trunc};
   while (auto chunk = co_await body_.async_read()) {
      output.write(reinterpret_cast<const char*>(chunk->bytes.data()), static_cast<std::streamsize>(chunk->bytes.size()));
   }
}

static_file_root::static_file_root(std::filesystem::path root, file_options options)
    : root_(std::filesystem::weakly_canonical(std::move(root))), options_(std::move(options)) {
   if (!std::filesystem::is_directory(root_)) {
      throw exceptions::bad_request{"static file root must be a directory"};
   }
}

const std::filesystem::path& static_file_root::root() const noexcept {
   return root_;
}

boost::asio::awaitable<stream_response> static_file_root::serve(stream_request& request_value,
                                                                std::string_view relative_path) const {
   try {
      auto resolved = resolve_contained_child(root_, relative_path, options_.symlinks);
      co_return co_await file_response::from_path(std::move(resolved), options_).materialize(request_value.context.request);
   } catch (const exceptions::forbidden&) {
      co_return stream_response::buffered(make_text_response(request_value.context.request, status::forbidden,
                                                            "forbidden"));
   }
}

} // namespace fcl::http
