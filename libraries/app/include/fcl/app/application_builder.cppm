module;

#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

export module fcl.app.application_builder;

import fcl.asio.runtime;
import fcl.asio.task_scheduler;
import fcl.config;
import fcl.app.application_shell;
import fcl.app.plugin_registry;

export namespace fcl::app {

class application_builder {
 public:
   application_builder();
   ~application_builder();

   application_builder(application_builder&&) noexcept;
   application_builder& operator=(application_builder&&) noexcept;

   application_builder(const application_builder&) = delete;
   application_builder& operator=(const application_builder&) = delete;

   application_builder& name(std::string value);
   application_builder& runtime(fcl::asio::runtime_options value);
   application_builder& scheduler(fcl::asio::task_scheduler::options value);

   application_builder& plugin(plugin_descriptor descriptor);
   application_builder& describe_config(fcl::config::component_descriptor descriptor);

   template <typename T, typename Handler> application_builder& config(std::string section, Handler&& handler) {
      auto descriptor = fcl::config::describe_component<T>(section);
      using handler_type = std::decay_t<Handler>;
      add_configure_callback(
         [section = std::move(section), handler = handler_type{std::forward<Handler>(handler)}](
            configure_context& context) mutable -> boost::asio::awaitable<void> {
            auto decoded = fcl::config::decode<T>(context.document(), section);
            if (!decoded.ok()) {
               throw make_decode_error(decoded.diagnostics);
            }

            if constexpr (std::is_invocable_v<handler_type&, configure_context&, const T&>) {
               using result_type = std::invoke_result_t<handler_type&, configure_context&, const T&>;
               if constexpr (std::same_as<std::remove_cvref_t<result_type>, boost::asio::awaitable<void>>) {
                  co_await std::invoke(handler, context, decoded.value);
               } else {
                  std::invoke(handler, context, decoded.value);
               }
            } else if constexpr (std::is_invocable_v<handler_type&, const T&>) {
               using result_type = std::invoke_result_t<handler_type&, const T&>;
               if constexpr (std::same_as<std::remove_cvref_t<result_type>, boost::asio::awaitable<void>>) {
                  co_await std::invoke(handler, decoded.value);
               } else {
                  std::invoke(handler, decoded.value);
               }
            } else {
               static_assert(dependent_false<handler_type>, "config handler must accept (configure_context&, const T&) or (const T&)");
            }
            co_return;
         });
      return describe_config(std::move(descriptor));
   }

   template <typename Handler> application_builder& configure(Handler&& handler) {
      using handler_type = std::decay_t<Handler>;
      add_configure_callback(
         [handler = handler_type{std::forward<Handler>(handler)}](
            configure_context& context) mutable -> boost::asio::awaitable<void> {
            if constexpr (std::is_invocable_v<handler_type&, configure_context&>) {
               using result_type = std::invoke_result_t<handler_type&, configure_context&>;
               if constexpr (std::same_as<std::remove_cvref_t<result_type>, boost::asio::awaitable<void>>) {
                  co_await std::invoke(handler, context);
               } else {
                  std::invoke(handler, context);
               }
            } else if constexpr (std::is_invocable_v<handler_type&>) {
               using result_type = std::invoke_result_t<handler_type&>;
               if constexpr (std::same_as<std::remove_cvref_t<result_type>, boost::asio::awaitable<void>>) {
                  co_await std::invoke(handler);
               } else {
                  std::invoke(handler);
               }
            } else {
               static_assert(dependent_false<handler_type>, "configure handler must accept configure_context& or no arguments");
            }
            co_return;
         });
      return *this;
   }

   template <typename Handler> application_builder& provide(Handler&& handler) {
      using handler_type = std::decay_t<Handler>;
      add_provide_callback(
         [handler = handler_type{std::forward<Handler>(handler)}](
            application_context& context) mutable -> boost::asio::awaitable<void> {
            if constexpr (std::is_invocable_v<handler_type&, application_context&>) {
               using result_type = std::invoke_result_t<handler_type&, application_context&>;
               if constexpr (std::same_as<std::remove_cvref_t<result_type>, boost::asio::awaitable<void>>) {
                  co_await std::invoke(handler, context);
               } else {
                  std::invoke(handler, context);
               }
            } else if constexpr (std::is_invocable_v<handler_type&>) {
               using result_type = std::invoke_result_t<handler_type&>;
               if constexpr (std::same_as<std::remove_cvref_t<result_type>, boost::asio::awaitable<void>>) {
                  co_await std::invoke(handler);
               } else {
                  std::invoke(handler);
               }
            } else {
               static_assert(dependent_false<handler_type>, "provide handler must accept application_context& or no arguments");
            }
            co_return;
         });
      return *this;
   }

   application_builder& run_foreground(std::function<int(application_shell&)> handler);

   [[nodiscard]] std::unique_ptr<application_shell> build() &&;

 private:
   using configure_callback = std::function<boost::asio::awaitable<void>(configure_context&)>;
   using provide_callback = std::function<boost::asio::awaitable<void>(application_context&)>;

   template <typename> static constexpr bool dependent_false = false;

   static std::invalid_argument make_decode_error(const fcl::config::decode_diagnostics& diagnostics);

   void add_configure_callback(configure_callback callback);
   void add_provide_callback(provide_callback callback);

   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace fcl::app
