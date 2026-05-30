#pragma once

namespace fcl::detail {

class evp_digest_context {
 public:
   evp_digest_context() : ctx_(EVP_MD_CTX_new()) {
      if (ctx_ == nullptr)
         FCL_THROW_EXCEPTION(fcl::crypto::digest::exceptions::backend_error, "error allocating EVP digest context",
                             fcl::exception::ctx("code", static_cast<uint32_t>(ERR_get_error())));
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
      FCL_THROW_EXCEPTION(fcl::crypto::digest::exceptions::backend_error, "error initializing EVP digest",
                          fcl::exception::ctx("code", static_cast<uint32_t>(ERR_get_error())));
}

inline void evp_digest_update(EVP_MD_CTX* ctx, const char* data, uint32_t size) {
   if (1 != EVP_DigestUpdate(ctx, data, size))
      FCL_THROW_EXCEPTION(fcl::crypto::digest::exceptions::backend_error, "error updating EVP digest",
                          fcl::exception::ctx("code", static_cast<uint32_t>(ERR_get_error())));
}

inline void evp_digest_final(EVP_MD_CTX* ctx, char* out, size_t expected_size) {
   unsigned int out_size = 0;
   if (1 != EVP_DigestFinal_ex(ctx, reinterpret_cast<unsigned char*>(out), &out_size))
      FCL_THROW_EXCEPTION(fcl::crypto::digest::exceptions::backend_error, "error finalizing EVP digest",
                          fcl::exception::ctx("code", static_cast<uint32_t>(ERR_get_error())));
   FCL_ASSERT(out_size == expected_size, "unexpected EVP digest size", fcl::exception::ctx("out_size", out_size),
              fcl::exception::ctx("expected_size", expected_size));
}

} // namespace fcl::detail
