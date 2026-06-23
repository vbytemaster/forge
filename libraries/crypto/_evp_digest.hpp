#pragma once

namespace forge::detail {

class evp_digest_context {
 public:
   evp_digest_context() : ctx_(EVP_MD_CTX_new()) {
      if (ctx_ == nullptr)
         FORGE_THROW_EXCEPTION(forge::crypto::digest::exceptions::backend_error, "error allocating EVP digest context",
                             forge::exceptions::ctx("code", static_cast<uint32_t>(ERR_get_error())));
   }

   evp_digest_context(const evp_digest_context&) = delete;
   evp_digest_context& operator=(const evp_digest_context&) = delete;

   ~evp_digest_context() {
      EVP_MD_CTX_free(ctx_);
   }

   EVP_MD_CTX* get() noexcept {
      return ctx_;
   }

 private:
   EVP_MD_CTX* ctx_ = nullptr;
};

inline void evp_digest_init(EVP_MD_CTX* ctx, const EVP_MD* md) {
   if (1 != EVP_DigestInit_ex(ctx, md, nullptr))
      FORGE_THROW_EXCEPTION(forge::crypto::digest::exceptions::backend_error, "error initializing EVP digest",
                          forge::exceptions::ctx("code", static_cast<uint32_t>(ERR_get_error())));
}

inline void evp_digest_update(EVP_MD_CTX* ctx, const char* data, uint32_t size) {
   if (1 != EVP_DigestUpdate(ctx, data, size))
      FORGE_THROW_EXCEPTION(forge::crypto::digest::exceptions::backend_error, "error updating EVP digest",
                          forge::exceptions::ctx("code", static_cast<uint32_t>(ERR_get_error())));
}

inline void evp_digest_final(EVP_MD_CTX* ctx, char* out, size_t expected_size) {
   unsigned int out_size = 0;
   if (1 != EVP_DigestFinal_ex(ctx, reinterpret_cast<unsigned char*>(out), &out_size))
      FORGE_THROW_EXCEPTION(forge::crypto::digest::exceptions::backend_error, "error finalizing EVP digest",
                          forge::exceptions::ctx("code", static_cast<uint32_t>(ERR_get_error())));
   FORGE_ASSERT(out_size == expected_size, "unexpected EVP digest size", forge::exceptions::ctx("out_size", out_size),
              forge::exceptions::ctx("expected_size", expected_size));
}

} // namespace forge::detail
