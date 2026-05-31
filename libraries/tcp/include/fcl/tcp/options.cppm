module;

#include <cstddef>

export module fcl.tcp.options;

export namespace fcl::tcp {

struct options {
   std::size_t read_chunk_size = 64 * 1024;
   bool no_delay = true;
   bool keep_alive = false;
   bool reuse_address = true;
};

} // namespace fcl::tcp
