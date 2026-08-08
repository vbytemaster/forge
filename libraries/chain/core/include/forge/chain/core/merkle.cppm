module;

#include <boost/describe.hpp>
#include <forge/exceptions/macros.hpp>

#include <bit>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module forge.chain.core.merkle;

export import forge.chain.core.types;
export import forge.chain.core.exceptions;
import forge.raw.exceptions;
import forge.raw.raw;

export namespace forge::chain::core {

struct merkle_step {
   digest sibling;
   bool sibling_on_left = false;

   bool operator==(const merkle_step&) const = default;
};

BOOST_DESCRIBE_STRUCT(merkle_step, (), (sibling, sibling_on_left))

digest calculate_merkle_root(std::span<const digest> leaves);
std::vector<merkle_step> calculate_merkle_path(std::span<const digest> leaves, std::uint64_t index);
bool verify_merkle_path(const digest& leaf, std::uint64_t index, std::uint64_t leaf_count,
                        std::span<const merkle_step> path, const digest& expected_root);

class incremental_merkle_tree {
 public:
   void append(const digest& leaf);

   [[nodiscard]] digest root() const;
   [[nodiscard]] std::uint64_t size() const noexcept;
   [[nodiscard]] bool empty() const noexcept;

 private:
   template <typename Stream> friend Stream& operator<<(Stream& stream, const incremental_merkle_tree& value) {
      forge::raw::pack(stream, value.mask_);
      forge::raw::pack(stream, value.trees_);
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, incremental_merkle_tree& value) {
      auto mask = std::uint64_t{};
      auto trees = std::vector<digest>{};
      forge::raw::unpack(stream, mask);
      forge::raw::unpack(stream, trees);

      if (trees.size() != static_cast<std::size_t>(std::popcount(mask))) {
         FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error,
                               "incremental merkle state does not match its leaf count",
                               forge::exceptions::ctx("mask", mask), forge::exceptions::ctx("trees", trees.size()));
      }

      value.mask_ = mask;
      value.trees_ = std::move(trees);
      return stream;
   }

   std::uint64_t mask_ = 0;
   std::vector<digest> trees_;
};

} // namespace forge::chain::core
