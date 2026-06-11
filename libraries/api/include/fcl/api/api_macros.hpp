#pragma once

#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include <memory>
#include <utility>

#define FCL_API_CONTRACT(ID, MAJOR, REVISION) (ID, MAJOR, REVISION)

#define FCL_API_METHOD(NAME) (NAME, void, void, 0, 0, "", 0)
#define FCL_API_METHOD_SINCE(NAME, REVISION) (NAME, void, void, REVISION, 0, "", 0)
#define FCL_API_METHOD_DEPRECATED(NAME, REASON) (NAME, void, void, 0, 1, REASON, 0)
#define FCL_API_METHOD_DEPRECATED_SINCE(NAME, REVISION, REASON) (NAME, void, void, REVISION, 1, REASON, 0)
#define FCL_API_METHOD_TYPED(NAME, REQUEST, RESPONSE) (NAME, REQUEST, RESPONSE, 0, 0, "", 1)
#define FCL_API_METHOD_TYPED_SINCE(NAME, REQUEST, RESPONSE, REVISION) (NAME, REQUEST, RESPONSE, REVISION, 0, "", 1)
#define FCL_API_METHOD_TYPED_DEPRECATED(NAME, REQUEST, RESPONSE, REASON) (NAME, REQUEST, RESPONSE, 0, 1, REASON, 1)
#define FCL_API_METHOD_TYPED_DEPRECATED_SINCE(NAME, REQUEST, RESPONSE, REVISION, REASON)                            \
   (NAME, REQUEST, RESPONSE, REVISION, 1, REASON, 1)

#if defined(__clang__)
#define FCL_API_DETAIL_DIAGNOSTIC_PUSH _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
#define FCL_API_DETAIL_DIAGNOSTIC_POP _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#define FCL_API_DETAIL_DIAGNOSTIC_PUSH _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define FCL_API_DETAIL_DIAGNOSTIC_POP _Pragma("GCC diagnostic pop")
#else
#define FCL_API_DETAIL_DIAGNOSTIC_PUSH
#define FCL_API_DETAIL_DIAGNOSTIC_POP
#endif

#define FCL_API_DETAIL_METHOD_NAME(METHOD) BOOST_PP_TUPLE_ELEM(7, 0, METHOD)
#define FCL_API_DETAIL_METHOD_REQUEST(METHOD) BOOST_PP_TUPLE_ELEM(7, 1, METHOD)
#define FCL_API_DETAIL_METHOD_RESPONSE(METHOD) BOOST_PP_TUPLE_ELEM(7, 2, METHOD)
#define FCL_API_DETAIL_METHOD_SINCE(METHOD) BOOST_PP_TUPLE_ELEM(7, 3, METHOD)
#define FCL_API_DETAIL_METHOD_DEPRECATED(METHOD) BOOST_PP_TUPLE_ELEM(7, 4, METHOD)
#define FCL_API_DETAIL_METHOD_REASON(METHOD) BOOST_PP_TUPLE_ELEM(7, 5, METHOD)
#define FCL_API_DETAIL_METHOD_TYPED(METHOD) BOOST_PP_TUPLE_ELEM(7, 6, METHOD)

#define FCL_API_DETAIL_TYPED_METHOD_POINTER(INTERFACE, METHOD)                                                     \
   static_cast<boost::asio::awaitable<FCL_API_DETAIL_METHOD_RESPONSE(METHOD)> (INTERFACE::*)(                     \
      FCL_API_DETAIL_METHOD_REQUEST(METHOD))>(&INTERFACE::FCL_API_DETAIL_METHOD_NAME(METHOD))

#define FCL_API_DETAIL_DESCRIPTOR_METHODS(INTERFACE, ...)                                                          \
   __VA_OPT__(BOOST_PP_SEQ_FOR_EACH(FCL_API_DETAIL_DESCRIPTOR_METHOD, INTERFACE,                                  \
                                    BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))

#define FCL_API_DETAIL_PROXY_METHODS(INTERFACE, ...)                                                               \
   __VA_OPT__(BOOST_PP_SEQ_FOR_EACH(FCL_API_DETAIL_PROXY_METHOD, INTERFACE, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))

#define FCL_API_DETAIL_DESCRIPTOR_METHOD(r, INTERFACE, METHOD)                                                     \
   BOOST_PP_IF(FCL_API_DETAIL_METHOD_TYPED(METHOD), FCL_API_DETAIL_DESCRIPTOR_METHOD_TYPED,                        \
               FCL_API_DETAIL_DESCRIPTOR_METHOD_DEDUCED)(r, INTERFACE, METHOD)

#define FCL_API_DETAIL_DESCRIPTOR_METHOD_DEDUCED(r, INTERFACE, METHOD)                                             \
   {                                                                                                               \
      auto method = builder.template method<&INTERFACE::FCL_API_DETAIL_METHOD_NAME(METHOD)>(                       \
         BOOST_PP_STRINGIZE(FCL_API_DETAIL_METHOD_NAME(METHOD)));                                                  \
      method.since_revision(FCL_API_DETAIL_METHOD_SINCE(METHOD));                                                  \
      BOOST_PP_IF(FCL_API_DETAIL_METHOD_DEPRECATED(METHOD),                                                        \
                  method.deprecated(FCL_API_DETAIL_METHOD_REASON(METHOD));, )                                      \
   }

#define FCL_API_DETAIL_DESCRIPTOR_METHOD_TYPED(r, INTERFACE, METHOD)                                               \
   {                                                                                                               \
      auto method = builder.template method<FCL_API_DETAIL_TYPED_METHOD_POINTER(INTERFACE, METHOD),                 \
                                            FCL_API_DETAIL_METHOD_REQUEST(METHOD),                                  \
                                            FCL_API_DETAIL_METHOD_RESPONSE(METHOD)>(                                \
         BOOST_PP_STRINGIZE(FCL_API_DETAIL_METHOD_NAME(METHOD)));                                                  \
      method.since_revision(FCL_API_DETAIL_METHOD_SINCE(METHOD));                                                  \
      BOOST_PP_IF(FCL_API_DETAIL_METHOD_DEPRECATED(METHOD),                                                        \
                  method.deprecated(FCL_API_DETAIL_METHOD_REASON(METHOD));, )                                      \
   }

#define FCL_API_DETAIL_PROXY_METHOD(r, INTERFACE, METHOD)                                                          \
   BOOST_PP_IF(FCL_API_DETAIL_METHOD_TYPED(METHOD), FCL_API_DETAIL_PROXY_METHOD_TYPED,                             \
               FCL_API_DETAIL_PROXY_METHOD_DEDUCED)(r, INTERFACE, METHOD)

#define FCL_API_DETAIL_PROXY_METHOD_DEDUCED(r, INTERFACE, METHOD)                                                  \
   boost::asio::awaitable<                                                                                         \
      ::fcl::api::method_response_t<&INTERFACE::FCL_API_DETAIL_METHOD_NAME(METHOD)>>                               \
   FCL_API_DETAIL_METHOD_NAME(METHOD)(                                                                             \
      ::fcl::api::method_request_t<&INTERFACE::FCL_API_DETAIL_METHOD_NAME(METHOD)> request) override {             \
      co_return co_await this->invoker_->template call<                                                            \
         ::fcl::api::method_request_t<&INTERFACE::FCL_API_DETAIL_METHOD_NAME(METHOD)>,                             \
         ::fcl::api::method_response_t<&INTERFACE::FCL_API_DETAIL_METHOD_NAME(METHOD)>>(                           \
            ::fcl::api::api_traits<INTERFACE>::describe(), this->selected_api_,                                    \
            BOOST_PP_STRINGIZE(FCL_API_DETAIL_METHOD_NAME(METHOD)), std::move(request));                           \
   }

#define FCL_API_DETAIL_PROXY_METHOD_TYPED(r, INTERFACE, METHOD)                                                    \
   boost::asio::awaitable<FCL_API_DETAIL_METHOD_RESPONSE(METHOD)>                                                  \
   FCL_API_DETAIL_METHOD_NAME(METHOD)(FCL_API_DETAIL_METHOD_REQUEST(METHOD) request) override {                    \
      co_return co_await this->invoker_->template call<FCL_API_DETAIL_METHOD_REQUEST(METHOD),                      \
                                                       FCL_API_DETAIL_METHOD_RESPONSE(METHOD)>(                     \
         ::fcl::api::api_traits<INTERFACE>::describe(), this->selected_api_,                                        \
         BOOST_PP_STRINGIZE(FCL_API_DETAIL_METHOD_NAME(METHOD)), std::move(request));                              \
   }

#define FCL_API(INTERFACE, CONTRACT, ...)                                                                          \
   FCL_API_DETAIL_DIAGNOSTIC_PUSH                                                                                  \
   namespace fcl::api {                                                                                            \
   template <> struct api_traits<INTERFACE> {                                                                      \
      static api_id id() {                                                                                         \
         return api_id{.value = BOOST_PP_TUPLE_ELEM(3, 0, CONTRACT)};                                              \
      }                                                                                                            \
      static api_version version() {                                                                               \
         return api_version{.major = BOOST_PP_TUPLE_ELEM(3, 1, CONTRACT),                                         \
                            .revision = BOOST_PP_TUPLE_ELEM(3, 2, CONTRACT)};                                     \
      }                                                                                                            \
      static api_ref ref(std::uint16_t min_revision = version().revision) {                                        \
         const auto value = version();                                                                             \
         return api_ref{.id = id(), .major = value.major, .min_revision = min_revision};                           \
      }                                                                                                            \
      static descriptor describe() {                                                                               \
         auto builder = ::fcl::api::define<INTERFACE>(                                                             \
            descriptor{.id = id(), .version = version(), .interface_type = typeid(INTERFACE)});                    \
         FCL_API_DETAIL_DESCRIPTOR_METHODS(INTERFACE, __VA_ARGS__)                                                 \
         return builder.build();                                                                                   \
      }                                                                                                            \
   };                                                                                                              \
   namespace detail {                                                                                             \
   template <> class proxy_impl<INTERFACE, true> : public INTERFACE {                                              \
    public:                                                                                                        \
      explicit proxy_impl(std::shared_ptr<remote_invoker> invoker, api_ref selected_api)                           \
          : invoker_(std::move(invoker)), selected_api_(std::move(selected_api)) {}                                \
      FCL_API_DETAIL_PROXY_METHODS(INTERFACE, __VA_ARGS__)                                                         \
    private:                                                                                                       \
      std::shared_ptr<remote_invoker> invoker_;                                                                    \
      api_ref selected_api_;                                                                                       \
   };                                                                                                              \
   }                                                                                                               \
   template <> class proxy<INTERFACE> final                                                                        \
       : public detail::proxy_impl<INTERFACE, remote_interface<INTERFACE>> {                                       \
    public:                                                                                                        \
      explicit proxy(std::shared_ptr<remote_invoker> invoker)                                                      \
          : proxy(std::move(invoker), INTERFACE::ref()) {}                                                         \
      explicit proxy(std::shared_ptr<remote_invoker> invoker, api_ref selected_api)                                \
          : detail::proxy_impl<INTERFACE, remote_interface<INTERFACE>>(std::move(invoker),                         \
                                                                       std::move(selected_api)) {}                 \
   };                                                                                                              \
   }                                                                                                               \
   FCL_API_DETAIL_DIAGNOSTIC_POP
