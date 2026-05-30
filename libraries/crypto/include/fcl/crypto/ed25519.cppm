module;
#include <fcl/exception/macros.hpp>
#include <array>
#include <boost/describe.hpp>
#include <cstdint>
#include <span>
#include <utility>

export module fcl.crypto.ed25519;

export import fcl.exception.exception;
import fcl.crypto.types;
import fcl.raw.raw;

export namespace fcl::crypto::ed25519 {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   backend_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.ed25519")

using invalid_key = fcl::exception::coded_exception<code, code::invalid_key>;
using backend_error = fcl::exception::coded_exception<code, code::backend_error>;

} // namespace exceptions

using public_key_data = std::array<std::uint8_t, 32>;
using private_key_secret = std::array<std::uint8_t, 32>;
using signature_data = std::array<std::uint8_t, 64>;

class public_key {
 public:
   public_key() = default;
   explicit public_key(const public_key_data& value);

   [[nodiscard]] const public_key_data& serialize() const noexcept;
   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] bool verify(std::span<const std::uint8_t> message, const signature_data& signature) const;

   friend bool operator==(const public_key&, const public_key&) = default;

 private:
   public_key_data data_{};
};

class private_key {
 public:
   private_key() = default;
   explicit private_key(const private_key_secret& value);

   [[nodiscard]] static private_key generate();
   [[nodiscard]] static private_key regenerate(const private_key_secret& value);

   [[nodiscard]] const private_key_secret& get_secret() const noexcept;
   [[nodiscard]] public_key get_public_key() const;
   [[nodiscard]] signature_data sign(std::span<const std::uint8_t> message) const;

   friend bool operator==(const private_key&, const private_key&) = default;

 private:
   private_key_secret data_{};
};

struct signature_shim;

struct public_key_shim {
   using data_type = public_key_data;
   using signature_type = signature_shim;

   public_key_shim() = default;
   explicit public_key_shim(const data_type& data) : _data(data) {}
   explicit public_key_shim(data_type&& data) : _data(std::move(data)) {}

   [[nodiscard]] const data_type& serialize() const {
      return _data;
   }

   template <typename Stream> friend Stream& operator<<(Stream& s, const public_key_shim& value) {
      fcl::raw::pack(s, value._data);
      return s;
   }

   template <typename Stream> friend Stream& operator>>(Stream& s, public_key_shim& value) {
      fcl::raw::unpack(s, value._data);
      return s;
   }

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] bool verify(std::span<const std::uint8_t> message, const signature_data& signature) const;

   data_type _data{};
};

struct signature_shim {
   using data_type = signature_data;
   using public_key_type = public_key_shim;

   signature_shim() = default;
   explicit signature_shim(const data_type& data) : _data(data) {}
   explicit signature_shim(data_type&& data) : _data(std::move(data)) {}

   [[nodiscard]] const data_type& serialize() const {
      return _data;
   }

   template <typename Stream> friend Stream& operator<<(Stream& s, const signature_shim& value) {
      fcl::raw::pack(s, value._data);
      return s;
   }

   template <typename Stream> friend Stream& operator>>(Stream& s, signature_shim& value) {
      fcl::raw::unpack(s, value._data);
      return s;
   }

   data_type _data{};
};

struct private_key_shim {
   using data_type = private_key_secret;
   using signature_type = signature_shim;
   using public_key_type = public_key_shim;

   private_key_shim() = default;
   explicit private_key_shim(const data_type& data) : _data(data) {}
   explicit private_key_shim(data_type&& data) : _data(std::move(data)) {}

   [[nodiscard]] const data_type& serialize() const {
      return _data;
   }

   template <typename Stream> friend Stream& operator<<(Stream& s, const private_key_shim& value) {
      fcl::raw::pack(s, value._data);
      return s;
   }

   template <typename Stream> friend Stream& operator>>(Stream& s, private_key_shim& value) {
      fcl::raw::unpack(s, value._data);
      return s;
   }

   [[nodiscard]] signature_type sign(std::span<const std::uint8_t> message) const;
   [[nodiscard]] public_key_type get_public_key() const;
   [[nodiscard]] static private_key_shim generate();

   data_type _data{};
};

BOOST_DESCRIBE_STRUCT(public_key_shim, (), (_data))
BOOST_DESCRIBE_STRUCT(signature_shim, (), (_data))
BOOST_DESCRIBE_STRUCT(private_key_shim, (), (_data))
} // namespace fcl::crypto::ed25519
