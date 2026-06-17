#pragma once

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include <memory>
#include <utility>
#include <vector>

#define FCL_HTTP_DETAIL_OPTION_KIND(OPTION) BOOST_PP_TUPLE_ELEM(3, 0, OPTION)
#define FCL_HTTP_DETAIL_OPTION_APPLY_header(OPTION)                                                                 \
   .header(BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, OPTION)), BOOST_PP_TUPLE_ELEM(3, 2, OPTION))
#define FCL_HTTP_DETAIL_OPTION_APPLY_form(OPTION)                                                                   \
   .form(BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, OPTION)), BOOST_PP_TUPLE_ELEM(3, 2, OPTION))
#define FCL_HTTP_DETAIL_OPTION_APPLY_body_stream(OPTION)                                                            \
   .body_stream(BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, OPTION)))
#define FCL_HTTP_DETAIL_OPTION_APPLY_response_file(OPTION) .response_file()
#define FCL_HTTP_DETAIL_OPTION_APPLY_response_stream(OPTION) .response_stream()
#define FCL_HTTP_DETAIL_OPTION_APPLY(r, DATA, OPTION)                                                               \
   BOOST_PP_CAT(FCL_HTTP_DETAIL_OPTION_APPLY_, FCL_HTTP_DETAIL_OPTION_KIND(OPTION))(OPTION)
#define FCL_HTTP_DETAIL_OPTIONS(...)                                                                                \
   __VA_OPT__(BOOST_PP_SEQ_FOR_EACH(FCL_HTTP_DETAIL_OPTION_APPLY, _, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))

#define FCL_HTTP_GET(NAME, TARGET, ...)                                                                             \
   (NAME, (::fcl::http::api_route_builder{::fcl::http::method::get, BOOST_PP_STRINGIZE(NAME), TARGET,               \
                                          ::fcl::http::status::ok} FCL_HTTP_DETAIL_OPTIONS(__VA_ARGS__))             \
             .build())
#define FCL_HTTP_HEAD(NAME, TARGET, ...)                                                                            \
   (NAME, (::fcl::http::api_route_builder{::fcl::http::method::head, BOOST_PP_STRINGIZE(NAME), TARGET,              \
                                          ::fcl::http::status::ok} FCL_HTTP_DETAIL_OPTIONS(__VA_ARGS__))             \
             .build())
#define FCL_HTTP_POST(NAME, TARGET, STATUS, ...)                                                                    \
   (NAME, (::fcl::http::api_route_builder{::fcl::http::method::post, BOOST_PP_STRINGIZE(NAME), TARGET,              \
                                          ::fcl::http::status::STATUS} FCL_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())
#define FCL_HTTP_PUT(NAME, TARGET, STATUS, ...)                                                                     \
   (NAME, (::fcl::http::api_route_builder{::fcl::http::method::put, BOOST_PP_STRINGIZE(NAME), TARGET,               \
                                          ::fcl::http::status::STATUS} FCL_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())
#define FCL_HTTP_PATCH(NAME, TARGET, STATUS, ...)                                                                   \
   (NAME, (::fcl::http::api_route_builder{::fcl::http::method::patch, BOOST_PP_STRINGIZE(NAME), TARGET,             \
                                          ::fcl::http::status::STATUS} FCL_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())
#define FCL_HTTP_DELETE(NAME, TARGET, STATUS, ...)                                                                  \
   (NAME, (::fcl::http::api_route_builder{::fcl::http::method::delete_, BOOST_PP_STRINGIZE(NAME), TARGET,           \
                                          ::fcl::http::status::STATUS} FCL_HTTP_DETAIL_OPTIONS(__VA_ARGS__))         \
             .build())

#define FCL_HTTP_HEADER(FIELD, NAME) (header, FIELD, NAME)
#define FCL_HTTP_FORM(FIELD, NAME) (form, FIELD, NAME)
#define FCL_HTTP_BODY_STREAM(FIELD) (body_stream, FIELD, _)
#define FCL_HTTP_RESPONSE_FILE (response_file, _, _)
#define FCL_HTTP_RESPONSE_STREAM (response_stream, _, _)

#define FCL_HTTP_DETAIL_METHOD(ROUTE) BOOST_PP_TUPLE_ELEM(2, 0, ROUTE)
#define FCL_HTTP_DETAIL_ROUTE_VALUE(ROUTE) BOOST_PP_TUPLE_ELEM(2, 1, ROUTE)

#define FCL_HTTP_DETAIL_ROUTE(ROUTE) FCL_HTTP_DETAIL_ROUTE_VALUE(ROUTE)

#define FCL_HTTP_DETAIL_ROUTE_ENTRY(r, INTERFACE, ROUTE) FCL_HTTP_DETAIL_ROUTE(ROUTE),

#define FCL_HTTP_DETAIL_BIND_ROUTE(r, INTERFACE, ROUTE)                                                             \
   builder.template route<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE),                                                \
                          ::fcl::api::method_request_t<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE)>,                  \
                          ::fcl::api::method_response_t<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE)>>(                \
      FCL_HTTP_DETAIL_ROUTE(ROUTE));

#define FCL_HTTP_DETAIL_PROXY_METHOD(r, INTERFACE, ROUTE)                                                           \
   boost::asio::awaitable<::fcl::api::method_response_t<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE)>>                 \
   FCL_HTTP_DETAIL_METHOD(ROUTE)(                                                                                   \
      ::fcl::api::method_request_t<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE)> request) override {                   \
      co_return co_await ::fcl::http::detail::call<                                                                 \
         ::fcl::api::method_request_t<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE)>,                                   \
         ::fcl::api::method_response_t<&INTERFACE::FCL_HTTP_DETAIL_METHOD(ROUTE)>>(                                 \
         *client_, ::fcl::api::api_traits<INTERFACE>::describe(), FCL_HTTP_DETAIL_ROUTE(ROUTE), std::move(request)); \
   }

#define FCL_HTTP_API(INTERFACE, ...)                                                                                \
   namespace fcl::http {                                                                                            \
   template <> struct http_api_traits<INTERFACE> {                                                                  \
      static std::vector<api_route> routes() {                                                                      \
         return ::fcl::http::detail::validate_routes(                                                               \
            ::fcl::api::api_traits<INTERFACE>::describe(),                                                          \
            std::vector<api_route>{BOOST_PP_SEQ_FOR_EACH(FCL_HTTP_DETAIL_ROUTE_ENTRY, INTERFACE,                    \
                                                         BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))});                  \
      }                                                                                                             \
      static api_builder& bind(api_builder& builder) {                                                              \
         static_cast<void>(routes());                                                                               \
         BOOST_PP_SEQ_FOR_EACH(FCL_HTTP_DETAIL_BIND_ROUTE, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))        \
         return builder;                                                                                            \
      }                                                                                                             \
   };                                                                                                               \
   template <> class proxy<INTERFACE> final : public INTERFACE {                                                    \
    public:                                                                                                         \
      explicit proxy(client& value) : client_(&value) {}                                                            \
      BOOST_PP_SEQ_FOR_EACH(FCL_HTTP_DETAIL_PROXY_METHOD, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))         \
    private:                                                                                                        \
      client* client_;                                                                                              \
   };                                                                                                               \
   }
