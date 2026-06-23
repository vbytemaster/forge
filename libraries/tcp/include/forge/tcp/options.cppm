module;

#include <cstddef>

export module forge.tcp.options;

export namespace forge::tcp {

struct options {
   std::size_t read_chunk_size = 64 * 1024;
   bool no_delay = true;
   bool keep_alive = false;
   bool reuse_address = true;
};

} // namespace forge::tcp
