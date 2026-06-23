module;
export module forge.crypto.packhash;

import forge.raw.raw;

export namespace forge::crypto {

template <typename Encoder, typename... T>
   requires(sizeof...(T) > 0)
auto packhash(const T&... t) {
   Encoder e;
   raw::pack(e, t...);
   return e.result();
}

template <typename Derived> struct add_packhash_to_hash {
   template <typename... T>
      requires(sizeof...(T) > 0)
   static auto packhash(const T&... args) {
      return forge::crypto::packhash<typename Derived::encoder>(args...);
   }

   friend auto operator<=>(const add_packhash_to_hash&, const add_packhash_to_hash&) = default;
};

} // namespace forge::crypto
