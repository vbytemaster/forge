#pragma once

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <exception>
#include <type_traits>
#include <utility>

namespace forge::db::authenticated::detail {

template <typename Awaitable> struct awaitable_value;

template <typename Value, typename Executor> struct awaitable_value<boost::asio::awaitable<Value, Executor>> {
   using type = Value;
};

template <typename Operation> auto invoke_backend(Operation&& operation) -> decltype(operation()) {
   using result_type = typename awaitable_value<decltype(operation())>::type;
   try {
      if constexpr (std::is_void_v<result_type>) {
         co_await std::forward<Operation>(operation)();
         co_return;
      } else {
         co_return co_await std::forward<Operation>(operation)();
      }
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::backend_failure, "authenticated database backend failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::backend_failure, "authenticated database backend failed");
   }
}

} // namespace forge::db::authenticated::detail
