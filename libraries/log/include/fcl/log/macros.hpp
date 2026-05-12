#pragma once

#include <boost/preprocessor/punctuation/paren.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>

#ifndef __func__
#define __func__ __FUNCTION__
#endif

/**
 * @def FCL_LOG_CONTEXT(LOG_LEVEL)
 * @brief Automatically captures the File, Line, and Method names and passes them to
 *        the constructor of fcl::log_context along with LOG_LEVEL
 * @param LOG_LEVEL - a valid log_level::Enum name.
 */
#define FCL_LOG_CONTEXT(LOG_LEVEL) fcl::log_context(fcl::log_level::LOG_LEVEL, __FILE__, __LINE__, __func__)

/**
 * @def FCL_LOG_MESSAGE(LOG_LEVEL,FORMAT,...)
 *
 * @brief A helper method for generating log messages.
 *
 * @param LOG_LEVEL a valid log_level::Enum name to be passed to the log_context
 * @param FORMAT A const char* string containing zero or more references to keys as "${key}"
 * @param ...  A set of key/value pairs denoted as ("key",val)("key2",val2)...
 */
#define FCL_LOG_MESSAGE(LOG_LEVEL, FORMAT, ...)                                                                        \
   fcl::log_message(FCL_LOG_CONTEXT(LOG_LEVEL), FORMAT, fcl::mutable_variant_object() __VA_ARGS__)

#define fcl_log(LOGGER, LOG_LEVEL, MESSAGE, ...)                                                                       \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(LOG_LEVEL))                                                                                 \
      (LOGGER).log((LOG_LEVEL), (MESSAGE), fcl::make_log_fields(__VA_ARGS__), std::source_location::current());        \
   FCL_MULTILINE_MACRO_END

// suppress warning "conditional expression is constant" in the while(0) for visual c++
// http://cnicholson.net/2009/03/stupid-c-tricks-dowhile0-and-c4127/
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

#define fcl_tlog(LOGGER, FORMAT, ...)                                                                                  \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(fcl::log_level::all))                                                                       \
      (LOGGER).log(FCL_LOG_MESSAGE(all, FORMAT, __VA_ARGS__));                                                         \
   FCL_MULTILINE_MACRO_END

#define fcl_dlog(LOGGER, FORMAT, ...)                                                                                  \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(fcl::log_level::debug))                                                                     \
      (LOGGER).log(FCL_LOG_MESSAGE(debug, FORMAT, __VA_ARGS__));                                                       \
   FCL_MULTILINE_MACRO_END

#define fcl_ilog(LOGGER, FORMAT, ...)                                                                                  \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(fcl::log_level::info))                                                                      \
      (LOGGER).log(FCL_LOG_MESSAGE(info, FORMAT, __VA_ARGS__));                                                        \
   FCL_MULTILINE_MACRO_END

#define fcl_wlog(LOGGER, FORMAT, ...)                                                                                  \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(fcl::log_level::warn))                                                                      \
      (LOGGER).log(FCL_LOG_MESSAGE(warn, FORMAT, __VA_ARGS__));                                                        \
   FCL_MULTILINE_MACRO_END

#define fcl_elog(LOGGER, FORMAT, ...)                                                                                  \
   FCL_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(fcl::log_level::error))                                                                     \
      (LOGGER).log(FCL_LOG_MESSAGE(error, FORMAT, __VA_ARGS__));                                                       \
   FCL_MULTILINE_MACRO_END

#define tlog(FORMAT, ...) fcl_tlog(fcl::logger::default_logger(), FORMAT, __VA_ARGS__)

#define dlog(FORMAT, ...) fcl_dlog(fcl::logger::default_logger(), FORMAT, __VA_ARGS__)

#define ilog(FORMAT, ...) fcl_ilog(fcl::logger::default_logger(), FORMAT, __VA_ARGS__)

#define wlog(FORMAT, ...) fcl_wlog(fcl::logger::default_logger(), FORMAT, __VA_ARGS__)

#define elog(FORMAT, ...) fcl_elog(fcl::logger::default_logger(), FORMAT, __VA_ARGS__)

#define FCL_FORMAT_ARG(r, unused, base) BOOST_PP_STRINGIZE(base) ": ${" BOOST_PP_STRINGIZE( base ) "} "

#define FCL_FORMAT_ARGS(r, unused, base)                                                                               \
   BOOST_PP_LPAREN() BOOST_PP_STRINGIZE(base), fcl::variant(base) BOOST_PP_RPAREN()

#define FCL_FORMAT(SEQ) BOOST_PP_SEQ_FOR_EACH(FCL_FORMAT_ARG, v, SEQ)

// takes a ... instead of a SEQ arg because it can be called with an empty SEQ
// from FCL_CAPTURE_AND_THROW()
#define FCL_FORMAT_ARG_PARAMS(...) BOOST_PP_SEQ_FOR_EACH(FCL_FORMAT_ARGS, v, __VA_ARGS__)

#define idump(SEQ) ilog(FCL_FORMAT(SEQ), FCL_FORMAT_ARG_PARAMS(SEQ))
#define wdump(SEQ) wlog(FCL_FORMAT(SEQ), FCL_FORMAT_ARG_PARAMS(SEQ))
#define edump(SEQ) elog(FCL_FORMAT(SEQ), FCL_FORMAT_ARG_PARAMS(SEQ))

// this disables all normal logging statements -- not something you'd normally want to do,
// but it's useful if you're benchmarking something and suspect logging is causing
// a slowdown.
#ifdef FCL_DISABLE_LOGGING
#undef ulog
#define ulog(...) FCL_MULTILINE_MACRO_BEGIN FCL_MULTILINE_MACRO_END
#undef elog
#define elog(...) FCL_MULTILINE_MACRO_BEGIN FCL_MULTILINE_MACRO_END
#undef wlog
#define wlog(...) FCL_MULTILINE_MACRO_BEGIN FCL_MULTILINE_MACRO_END
#undef ilog
#define ilog(...) FCL_MULTILINE_MACRO_BEGIN FCL_MULTILINE_MACRO_END
#undef dlog
#define dlog(...) FCL_MULTILINE_MACRO_BEGIN FCL_MULTILINE_MACRO_END
#endif
