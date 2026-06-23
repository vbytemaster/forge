#pragma once

#include <boost/preprocessor/punctuation/paren.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>

#ifndef __func__
#define __func__ __FUNCTION__
#endif

/**
 * @def FORGE_LOG_CONTEXT(LOG_LEVEL)
 * @brief Automatically captures the File, Line, and Method names and passes them to
 *        the constructor of forge::log_context along with LOG_LEVEL
 * @param LOG_LEVEL - a valid log_level::Enum name.
 */
#define FORGE_LOG_CONTEXT(LOG_LEVEL) forge::log_context(forge::log_level::LOG_LEVEL, __FILE__, __LINE__, __func__)

/**
 * @def FORGE_LOG_MESSAGE(LOG_LEVEL,FORMAT,...)
 *
 * @brief A helper method for generating log messages.
 *
 * @param LOG_LEVEL a valid log_level::Enum name to be passed to the log_context
 * @param FORMAT A const char* string containing zero or more references to keys as "${key}"
 * @param ...  A set of key/value pairs denoted as ("key",val)("key2",val2)...
 */
#define FORGE_LOG_MESSAGE(LOG_LEVEL, FORMAT, ...)                                                                        \
   forge::log_message(FORGE_LOG_CONTEXT(LOG_LEVEL), FORMAT, forge::mutable_variant_object() __VA_ARGS__)

#define forge_log(LOGGER, LOG_LEVEL, MESSAGE, ...)                                                                       \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(LOG_LEVEL))                                                                                 \
      (LOGGER).log((LOG_LEVEL), (MESSAGE), forge::make_log_fields(__VA_ARGS__), std::source_location::current());        \
   FORGE_MULTILINE_MACRO_END

// suppress warning "conditional expression is constant" in the while(0) for visual c++
// http://cnicholson.net/2009/03/stupid-c-tricks-dowhile0-and-c4127/
#define FORGE_MULTILINE_MACRO_BEGIN do {
#ifdef _MSC_VER
#define FORGE_MULTILINE_MACRO_END                                                                                        \
   __pragma(warning(push)) __pragma(warning(disable : 4127))                                                           \
   }                                                                                                                   \
   while (0)                                                                                                           \
   __pragma(warning(pop))
#else
#define FORGE_MULTILINE_MACRO_END                                                                                        \
   }                                                                                                                   \
   while (0)
#endif

#define forge_tlog(LOGGER, FORMAT, ...)                                                                                  \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(forge::log_level::all))                                                                       \
      (LOGGER).log(FORGE_LOG_MESSAGE(all, FORMAT, __VA_ARGS__));                                                         \
   FORGE_MULTILINE_MACRO_END

#define forge_dlog(LOGGER, FORMAT, ...)                                                                                  \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(forge::log_level::debug))                                                                     \
      (LOGGER).log(FORGE_LOG_MESSAGE(debug, FORMAT, __VA_ARGS__));                                                       \
   FORGE_MULTILINE_MACRO_END

#define forge_ilog(LOGGER, FORMAT, ...)                                                                                  \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(forge::log_level::info))                                                                      \
      (LOGGER).log(FORGE_LOG_MESSAGE(info, FORMAT, __VA_ARGS__));                                                        \
   FORGE_MULTILINE_MACRO_END

#define forge_wlog(LOGGER, FORMAT, ...)                                                                                  \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(forge::log_level::warn))                                                                      \
      (LOGGER).log(FORGE_LOG_MESSAGE(warn, FORMAT, __VA_ARGS__));                                                        \
   FORGE_MULTILINE_MACRO_END

#define forge_elog(LOGGER, FORMAT, ...)                                                                                  \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                           \
   if ((LOGGER).is_enabled(forge::log_level::error))                                                                     \
      (LOGGER).log(FORGE_LOG_MESSAGE(error, FORMAT, __VA_ARGS__));                                                       \
   FORGE_MULTILINE_MACRO_END

#define tlog(FORMAT, ...) forge_tlog(forge::logger::default_logger(), FORMAT, __VA_ARGS__)

#define dlog(FORMAT, ...) forge_dlog(forge::logger::default_logger(), FORMAT, __VA_ARGS__)

#define ilog(FORMAT, ...) forge_ilog(forge::logger::default_logger(), FORMAT, __VA_ARGS__)

#define wlog(FORMAT, ...) forge_wlog(forge::logger::default_logger(), FORMAT, __VA_ARGS__)

#define elog(FORMAT, ...) forge_elog(forge::logger::default_logger(), FORMAT, __VA_ARGS__)

#define FORGE_FORMAT_ARG(r, unused, base) BOOST_PP_STRINGIZE(base) ": ${" BOOST_PP_STRINGIZE( base ) "} "

#define FORGE_FORMAT_ARGS(r, unused, base)                                                                               \
   BOOST_PP_LPAREN() BOOST_PP_STRINGIZE(base), forge::variant(base) BOOST_PP_RPAREN()

#define FORGE_FORMAT(SEQ) BOOST_PP_SEQ_FOR_EACH(FORGE_FORMAT_ARG, v, SEQ)

// takes a ... instead of a SEQ arg because it can be called with an empty SEQ
// from FORGE_CAPTURE_AND_THROW()
#define FORGE_FORMAT_ARG_PARAMS(...) BOOST_PP_SEQ_FOR_EACH(FORGE_FORMAT_ARGS, v, __VA_ARGS__)

#define idump(SEQ) ilog(FORGE_FORMAT(SEQ), FORGE_FORMAT_ARG_PARAMS(SEQ))
#define wdump(SEQ) wlog(FORGE_FORMAT(SEQ), FORGE_FORMAT_ARG_PARAMS(SEQ))
#define edump(SEQ) elog(FORGE_FORMAT(SEQ), FORGE_FORMAT_ARG_PARAMS(SEQ))

// this disables all normal logging statements -- not something you'd normally want to do,
// but it's useful if you're benchmarking something and suspect logging is causing
// a slowdown.
#ifdef FORGE_DISABLE_LOGGING
#undef ulog
#define ulog(...) FORGE_MULTILINE_MACRO_BEGIN FORGE_MULTILINE_MACRO_END
#undef elog
#define elog(...) FORGE_MULTILINE_MACRO_BEGIN FORGE_MULTILINE_MACRO_END
#undef wlog
#define wlog(...) FORGE_MULTILINE_MACRO_BEGIN FORGE_MULTILINE_MACRO_END
#undef ilog
#define ilog(...) FORGE_MULTILINE_MACRO_BEGIN FORGE_MULTILINE_MACRO_END
#undef dlog
#define dlog(...) FORGE_MULTILINE_MACRO_BEGIN FORGE_MULTILINE_MACRO_END
#endif
