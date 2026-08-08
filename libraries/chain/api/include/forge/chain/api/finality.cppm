module;

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

export module forge.chain.api.finality;

export import forge.chain.protocol.audit;

export namespace forge::chain::api {

class finality_verifier {
 public:
   virtual ~finality_verifier() = default;

   [[nodiscard]] virtual std::optional<protocol::block_id> preferred_trust_anchor() const;

   virtual void verify(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) = 0;

   virtual void verify_ancestry(const protocol::state_anchor& finalized,
                                std::span<const protocol::state_anchor> intermediate,
                                const protocol::proof_blob& proof) = 0;
};

class cached_finality_verifier final : public finality_verifier {
 public:
   cached_finality_verifier(std::shared_ptr<finality_verifier> delegate, std::size_t capacity);
   ~cached_finality_verifier();

   cached_finality_verifier(const cached_finality_verifier&) = delete;
   cached_finality_verifier& operator=(const cached_finality_verifier&) = delete;
   cached_finality_verifier(cached_finality_verifier&&) noexcept;
   cached_finality_verifier& operator=(cached_finality_verifier&&) noexcept;

   [[nodiscard]] std::optional<protocol::block_id> preferred_trust_anchor() const override;

   void verify(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) override;

   void verify_ancestry(const protocol::state_anchor& finalized, std::span<const protocol::state_anchor> intermediate,
                        const protocol::proof_blob& proof) override;

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::chain::api
