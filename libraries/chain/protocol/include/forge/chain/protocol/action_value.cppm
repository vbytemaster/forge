module;

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.chain.protocol.action:value;

export import forge.chain.protocol.types;

import forge.raw.codec;

namespace forge::chain::protocol::details {

template <typename Data>
using named_action_name = std::integral_constant<std::uint64_t, std::remove_cvref_t<Data>::get_name().value>;

template <typename Data>
concept named_action_payload = requires {
   static_cast<action_name (*)()>(&std::remove_cvref_t<Data>::get_name);
   typename named_action_name<Data>;
};

} // namespace forge::chain::protocol::details

export namespace forge::chain::protocol {

struct action_base {
   account_name account;
   action_name name;
   std::vector<permission_level> authorization;

   bool operator==(const action_base&) const = default;
};

struct action : action_base {
   bytes data;

   action() = default;

   bool operator==(const action&) const = default;

   template <typename Data>
   action(std::vector<permission_level> permissions, account_name raw_account, action_name raw_name, Data&& value) {
      account = raw_account;
      name = raw_name;
      authorization = std::move(permissions);
      data = forge::raw::pack(std::forward<Data>(value));
   }

   template <typename Data>
   action(permission_level permission, account_name raw_account, action_name raw_name, Data&& value)
       : action(std::vector<permission_level>{permission}, raw_account, raw_name, std::forward<Data>(value)) {}

   template <typename Data>
      requires details::named_action_payload<Data>
   action(std::vector<permission_level> permissions, account_name raw_account, Data&& value)
       : action(std::move(permissions), raw_account, action_name{details::named_action_name<Data>::value},
                std::forward<Data>(value)) {}

   template <typename Data>
      requires details::named_action_payload<Data>
   action(permission_level permission, account_name raw_account, Data&& value)
       : action(std::vector<permission_level>{permission}, raw_account, std::forward<Data>(value)) {}
};

template <typename Stream> void raw_pack(Stream& stream, const action_base& value) {
   forge::raw::pack(stream, value.account);
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.authorization);
}

template <typename Stream> void raw_unpack(Stream& stream, action_base& value) {
   forge::raw::unpack(stream, value.account);
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.authorization);
}

template <typename Stream> void raw_pack(Stream& stream, const action& value) {
   raw_pack(stream, static_cast<const action_base&>(value));
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, action& value) {
   raw_unpack(stream, static_cast<action_base&>(value));
   forge::raw::unpack(stream, value.data);
}

} // namespace forge::chain::protocol
