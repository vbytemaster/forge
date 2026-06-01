#pragma once

#include <chrono>
#include <source_location>
#include <string>
#include <system_error>
#include <type_traits>

#ifndef __func__
#define __func__ __FUNCTION__
#endif

#if __APPLE__
#define LIKELY(x) __builtin_expect((long)!!(x), 1L)
#define UNLIKELY(x) __builtin_expect((long)!!(x), 0L)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#ifndef FCL_MULTILINE_MACRO_BEGIN
#define FCL_MULTILINE_MACRO_BEGIN do {
#ifdef _MSC_VER
#define FCL_MULTILINE_MACRO_END                                                                                        \
   __pragma(warning(push)) __pragma(warning(disable : 4127))                                                           \
   }                                                                                                                   \
   while (0)                                                                                                           \
   __pragma(warning(pop))
#else
#define FCL_MULTILINE_MACRO_END                                                                                        \
   }                                                                                                                   \
   while (0)
#endif
#endif

#define FCL_THROW(MESSAGE, ...)                                                                                        \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   fcl::exceptions::throw_with_context(MESSAGE, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);            \
   FCL_MULTILINE_MACRO_END

#define FCL_DECLARE_EXCEPTION_CATEGORY(Enum, CATEGORY_NAME)                                                            \
   inline const std::error_category& fcl_exceptions_category(Enum) noexcept {                                           \
      static const fcl::exceptions::category instance{CATEGORY_NAME};                                                    \
      return instance;                                                                                                 \
   }                                                                                                                   \
   inline std::error_code make_error_code(Enum value) noexcept {                                                       \
      return std::error_code{static_cast<int>(value), fcl_exceptions_category(value)};                                   \
   }

#define FCL_THROW_EXCEPTION(ExceptionType, MESSAGE, ...)                                                               \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   static_assert(std::is_base_of_v<fcl::exceptions::base, ExceptionType>,                                               \
                 "FCL_THROW_EXCEPTION expects a type derived from fcl::exceptions::base");                             \
   throw ExceptionType(std::string(MESSAGE),                                                                           \
                       fcl::exceptions::make_fields(__VA_ARGS__),                                                       \
                       std::source_location::current());                                                               \
   FCL_MULTILINE_MACRO_END

#define FCL_THROW_CODE(CodeValue, MESSAGE, ...)                                                                        \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   const auto fcl_exceptions_code_value = (CodeValue);                                                                  \
   fcl::exceptions::throw_code(fcl_exceptions_code_value,                                                                 \
                              std::string(MESSAGE),                                                                    \
                              fcl::exceptions::make_fields(__VA_ARGS__),                                                \
                              std::source_location::current());                                                        \
   FCL_MULTILINE_MACRO_END

#define FCL_ASSERT(TEST, ...)                                                                                          \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if (UNLIKELY(!(TEST))) {                                                                                            \
      fcl::exceptions::throw_assertion_failure(#TEST, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);      \
   }                                                                                                                   \
   FCL_MULTILINE_MACRO_END

#define FCL_CHECK_DEADLINE(DEADLINE, ...)                                                                              \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   const auto fcl_deadline_value = (DEADLINE);                                                                         \
   if (fcl_deadline_value < decltype(fcl_deadline_value)::max() &&                                                     \
       fcl_deadline_value < decltype(fcl_deadline_value)::clock::now()) {                                              \
      fcl::exceptions::throw_deadline_exceeded("deadline exceeded",                                                     \
                                              std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);             \
   }                                                                                                                   \
   FCL_MULTILINE_MACRO_END

#define FCL_CAPTURE_AND_RETHROW(MESSAGE, ...)                                                                          \
   catch (...) {                                                                                                       \
      fcl::exceptions::capture_and_rethrow(MESSAGE, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);        \
   }

#define FCL_CAPTURE_LOG_AND_RETHROW(MESSAGE, ...)                                                                      \
   catch (...) {                                                                                                       \
      fcl::exceptions::capture_and_log(MESSAGE __VA_OPT__(, ) __VA_ARGS__);                                             \
      fcl::exceptions::capture_and_rethrow(MESSAGE, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);        \
   }

#define FCL_CAPTURE_AND_LOG(MESSAGE, ...)                                                                              \
   catch (...) {                                                                                                       \
      fcl::exceptions::capture_and_log(MESSAGE __VA_OPT__(, ) __VA_ARGS__);                                             \
   }

#define FCL_LOG_AND_RETHROW() FCL_CAPTURE_AND_RETHROW("rethrow")
#define FCL_LOG_AND_DROP(...) FCL_CAPTURE_AND_LOG("drop" __VA_OPT__(, ) __VA_ARGS__)
