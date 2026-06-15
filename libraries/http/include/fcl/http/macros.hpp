#pragma once

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include <memory>
#include <utility>
#include <vector>

#define FCL_HTTP_GET(NAME, TARGET) (get, NAME, TARGET, ok)
#define FCL_HTTP_POST(NAME, TARGET, STATUS) (post, NAME, TARGET, STATUS)
#define FCL_HTTP_PUT(NAME, TARGET, STATUS) (put, NAME, TARGET, STATUS)
#define FCL_HTTP_PATCH(NAME, TARGET, STATUS) (patch, NAME, TARGET, STATUS)
#define FCL_HTTP_DELETE(NAME, TARGET, STATUS) (delete_, NAME, TARGET, STATUS)

#define FCL_HTTP_DETAIL_VERB(ROUTE) BOOST_PP_TUPLE_ELEM(4, 0, ROUTE)
#define FCL_HTTP_DETAIL_METHOD(ROUTE) BOOST_PP_TUPLE_ELEM(4, 1, ROUTE)
#define FCL_HTTP_DETAIL_TARGET(ROUTE) BOOST_PP_TUPLE_ELEM(4, 2, ROUTE)
#define FCL_HTTP_DETAIL_STATUS(ROUTE) BOOST_PP_TUPLE_ELEM(4, 3, ROUTE)

#define FCL_HTTP_DETAIL_ROUTE(ROUTE)                                                                                \
   ::fcl::http::api_route {                                                                                         \
      .verb = ::fcl::http::method::FCL_HTTP_DETAIL_VERB(ROUTE),                                                      \
      .method_name = BOOST_PP_STRINGIZE(FCL_HTTP_DETAIL_METHOD(ROUTE)),                                              \
      .target = FCL_HTTP_DETAIL_TARGET(ROUTE),                                                                       \
      .success_status = ::fcl::http::status::FCL_HTTP_DETAIL_STATUS(ROUTE),                                          \
   }

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
