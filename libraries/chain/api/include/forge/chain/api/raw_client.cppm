module;

#include <memory>

export module forge.chain.api.raw_client;

export import forge.chain.api.block;
export import forge.chain.api.info;
export import forge.chain.api.state;
export import forge.chain.api.transaction;

import forge.api.core.handle;

export namespace forge::chain::api {

struct service_handles {
   forge::api::core::handle<info> information;
   forge::api::core::handle<block> blocks;
   forge::api::core::handle<state> state_queries;
   forge::api::core::handle<transaction> transactions;
};

class raw_client {
 public:
   explicit raw_client(service_handles services);

   [[nodiscard]] const service_handles& services() const noexcept;
   [[nodiscard]] forge::chain::api::info& info() const;
   [[nodiscard]] forge::chain::api::block& blocks() const;
   [[nodiscard]] forge::chain::api::state& state() const;
   [[nodiscard]] forge::chain::api::transaction& transactions() const;

 private:
   service_handles services_;
};

} // namespace forge::chain::api
