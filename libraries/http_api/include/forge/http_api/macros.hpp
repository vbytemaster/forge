#pragma once

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#define FORGE_HTTP_DETAIL_OPTION_KIND(OPTION) BOOST_PP_TUPLE_ELEM(3, 0, OPTION)
#define FORGE_HTTP_DETAIL_OPTION_APPLY_header(OPTION)                                                                 \
   .header(BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, OPTION)), BOOST_PP_TUPLE_ELEM(3, 2, OPTION))
#define FORGE_HTTP_DETAIL_OPTION_APPLY_form(OPTION)                                                                   \
   .form(BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, OPTION)), BOOST_PP_TUPLE_ELEM(3, 2, OPTION))
#define FORGE_HTTP_DETAIL_OPTION_APPLY_body_stream(OPTION)                                                            \
   .body_stream(BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, OPTION)))
#define FORGE_HTTP_DETAIL_OPTION_APPLY_response_file(OPTION) .response_file()
#define FORGE_HTTP_DETAIL_OPTION_APPLY_response_stream(OPTION) .response_stream()
#define FORGE_HTTP_DETAIL_OPTION_APPLY(r, DATA, OPTION)                                                               \
   BOOST_PP_CAT(FORGE_HTTP_DETAIL_OPTION_APPLY_, FORGE_HTTP_DETAIL_OPTION_KIND(OPTION))(OPTION)
#define FORGE_HTTP_DETAIL_OPTIONS(...)                                                                                \
   __VA_OPT__(BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_OPTION_APPLY, _, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))

#define FORGE_HTTP_GET(NAME, TARGET, ...)                                                                             \
   (NAME, (::forge::http::api::route_builder{::forge::http::method::get, BOOST_PP_STRINGIZE(NAME), TARGET,               \
                                          ::forge::http::status::ok} FORGE_HTTP_DETAIL_OPTIONS(__VA_ARGS__))             \
             .build())
#define FORGE_HTTP_HEAD(NAME, TARGET, ...)                                                                            \
   (NAME, (::forge::http::api::route_builder{::forge::http::method::head, BOOST_PP_STRINGIZE(NAME), TARGET,              \
                                          ::forge::http::status::ok} FORGE_HTTP_DETAIL_OPTIONS(__VA_ARGS__))             \
             .build())
#define FORGE_HTTP_POST(NAME, TARGET, STATUS, ...)                                                                    \
   (NAME, (::forge::http::api::route_builder{::forge::http::method::post, BOOST_PP_STRINGIZE(NAME), TARGET,              \
                                          ::forge::http::status::STATUS} FORGE_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())
#define FORGE_HTTP_PUT(NAME, TARGET, STATUS, ...)                                                                     \
   (NAME, (::forge::http::api::route_builder{::forge::http::method::put, BOOST_PP_STRINGIZE(NAME), TARGET,               \
                                          ::forge::http::status::STATUS} FORGE_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())
#define FORGE_HTTP_PATCH(NAME, TARGET, STATUS, ...)                                                                   \
   (NAME, (::forge::http::api::route_builder{::forge::http::method::patch, BOOST_PP_STRINGIZE(NAME), TARGET,             \
                                          ::forge::http::status::STATUS} FORGE_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())
#define FORGE_HTTP_DELETE(NAME, TARGET, STATUS, ...)                                                                  \
   (NAME, (::forge::http::api::route_builder{::forge::http::method::delete_, BOOST_PP_STRINGIZE(NAME), TARGET,           \
                                          ::forge::http::status::STATUS} FORGE_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())

#define FORGE_HTTP_HEADER(FIELD, NAME) (header, FIELD, NAME)
#define FORGE_HTTP_FORM(FIELD, NAME) (form, FIELD, NAME)
#define FORGE_HTTP_BODY_STREAM(FIELD) (body_stream, FIELD, _)
#define FORGE_HTTP_RESPONSE_FILE (response_file, _, _)
#define FORGE_HTTP_RESPONSE_STREAM (response_stream, _, _)

#define FORGE_HTTP_DETAIL_METHOD(ROUTE) BOOST_PP_TUPLE_ELEM(2, 0, ROUTE)
#define FORGE_HTTP_DETAIL_ROUTE_VALUE(ROUTE) BOOST_PP_TUPLE_ELEM(2, 1, ROUTE)

#define FORGE_HTTP_DETAIL_ROUTE(ROUTE) FORGE_HTTP_DETAIL_ROUTE_VALUE(ROUTE)

#define FORGE_HTTP_DETAIL_ROUTE_ENTRY(r, INTERFACE, ROUTE) FORGE_HTTP_DETAIL_ROUTE(ROUTE),

#define FORGE_HTTP_DETAIL_BIND_ROUTE(r, INTERFACE, ROUTE)                                                             \
   builder.template route<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE),                                                \
                          ::forge::api::method_request_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>,                  \
                          ::forge::api::method_response_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>(                \
      FORGE_HTTP_DETAIL_ROUTE(ROUTE));

#define FORGE_HTTP_DETAIL_ROUTE_CALL(r, INTERFACE, ROUTE)                                                             \
   ::forge::http::api::detail::make_route_call<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE),                                  \
                                        ::forge::api::method_request_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>,     \
                                        ::forge::api::method_response_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>(   \
      FORGE_HTTP_DETAIL_ROUTE(ROUTE)),

#define FORGE_HTTP_DETAIL_ROUTE_API_PROXY_SUPPORTED(r, INTERFACE, ROUTE)                                              \
   && ::forge::http::api::detail::route_can_use_api_proxy_v<                                                               \
         &INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE),                                                                \
         ::forge::api::method_request_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>,                                   \
         ::forge::api::method_response_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>

#define FORGE_HTTP_DETAIL_PROXY_USING(r, INTERFACE, ROUTE)                                                           \
   using ::forge::api::proxy<INTERFACE>::FORGE_HTTP_DETAIL_METHOD(ROUTE);

#define FORGE_HTTP_DETAIL_PROXY_METHOD(r, INTERFACE, ROUTE)                                                           \
   boost::asio::awaitable<::forge::api::method_response_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>                 \
   FORGE_HTTP_DETAIL_METHOD(ROUTE)(                                                                                   \
      ::forge::api::method_request_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)> request) {                            \
      if constexpr (::forge::api::method_argument_count_v<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)> == 1U) {         \
         using request_type =                                                                                        \
            std::remove_cvref_t<::forge::api::method_request_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>;           \
         if constexpr (::forge::http::api::detail::is_positional_http_method_v<                                            \
                          &INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE), request_type>) {                              \
            auto arguments = std::make_tuple(std::move(request));                                                   \
            co_return co_await ::forge::http::api::detail::call_arguments<decltype(arguments),                             \
               ::forge::api::method_response_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>(                           \
               *client_, ::forge::api::api_traits<INTERFACE>::describe(), FORGE_HTTP_DETAIL_ROUTE(ROUTE),               \
               std::move(arguments),                                                                                \
               ::forge::http::api::detail::argument_names_for(::forge::api::api_traits<INTERFACE>::describe(),               \
                                                       BOOST_PP_STRINGIZE(FORGE_HTTP_DETAIL_METHOD(ROUTE))));         \
         } else {                                                                                                   \
            co_return co_await ::forge::http::api::detail::call<                                                           \
               ::forge::api::method_request_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>,                             \
               ::forge::api::method_response_t<&INTERFACE::FORGE_HTTP_DETAIL_METHOD(ROUTE)>>(                           \
               *client_, ::forge::api::api_traits<INTERFACE>::describe(), FORGE_HTTP_DETAIL_ROUTE(ROUTE),               \
               std::move(request));                                                                                 \
         }                                                                                                          \
      } else {                                                                                                      \
         FORGE_THROW_EXCEPTION(::forge::http::exceptions::bad_request,                                                   \
                             "HTTP positional proxy requires forge::api remote proxy");                              \
      }                                                                                                             \
   }

#define FORGE_HTTP_API(INTERFACE, ...)                                                                                \
   namespace forge::http::api {                                                                                            \
   template <> struct traits<INTERFACE> {                                                                  \
      static std::vector<route> routes() {                                                                      \
         return ::forge::http::api::detail::validate_routes(                                                               \
            ::forge::api::api_traits<INTERFACE>::describe(),                                                          \
            std::vector<route>{BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_ROUTE_ENTRY, INTERFACE,                    \
                                                         BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))});                  \
      }                                                                                                             \
      static binding_builder& bind(binding_builder& builder) {                                                              \
         static_cast<void>(routes());                                                                               \
         BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_BIND_ROUTE, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))        \
         return builder;                                                                                            \
      }                                                                                                             \
      static constexpr bool use_api_proxy = true                                                                     \
         BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_ROUTE_API_PROXY_SUPPORTED, INTERFACE,                                \
                               BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__));                                               \
      static std::shared_ptr<::forge::api::remote_invoker> make_invoker(::forge::http::client& value) {                              \
         return ::forge::http::api::detail::make_route_invoker(                                                            \
            value, ::forge::api::api_traits<INTERFACE>::describe(),                                                    \
            std::vector<::forge::http::api::detail::route_call>{                                                           \
               BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_ROUTE_CALL, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))  \
            });                                                                                                     \
      }                                                                                                             \
   };                                                                                                               \
   template <> class proxy<INTERFACE> : public ::forge::api::proxy<INTERFACE> {                                      \
    public:                                                                                                         \
      explicit proxy(::forge::http::client& value)                                                                    \
          : ::forge::api::proxy<INTERFACE>(traits<INTERFACE>::make_invoker(value), INTERFACE::ref()), client_(&value) {} \
      BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_PROXY_USING, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))          \
      BOOST_PP_SEQ_FOR_EACH(FORGE_HTTP_DETAIL_PROXY_METHOD, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))         \
    private:                                                                                                        \
      ::forge::http::client* client_;                                                                                              \
   };                                                                                                               \
   }
