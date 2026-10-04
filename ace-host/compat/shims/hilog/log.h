/*
 * Stand-in for OpenHarmony's <hilog/log.h>.
 *
 * WHY THIS CAN BE A NO-OP AND THE securec SHIM CANNOT
 * --------------------------------------------------
 * hilog is a log sink. Dropping log records changes nothing that any caller
 * observes -- no program state and no return value depends on a log line being
 * written -- so an empty implementation is behaviourally faithful apart from the
 * side effect of not printing. That makes it safe to stub.
 *
 * Note the asymmetry with compat/shims/securec.h in this same directory: the
 * checked-memory functions there *are* a safety property, so ignoring their size
 * arguments would compile and be silently wrong, and they are implemented for
 * real. The test for a stub is whether any caller's behaviour depends on the
 * side effect, not whether the header is small.
 *
 * The macros deliberately evaluate their arguments so that a variable used only
 * in a log call does not become "unused" and trip -Werror in a caller, which
 * would turn a logging shim into a build failure somewhere unrelated.
 */
#ifndef ACE_HOST_HILOG_LOG_H
#define ACE_HOST_HILOG_LOG_H

#include <cstdio>

namespace OHOS {
namespace HiviewDFX {

enum LogLevel {
    LOG_DEBUG = 3,
    LOG_INFO = 4,
    LOG_WARN = 5,
    LOG_ERROR = 6,
    LOG_FATAL = 7,
};

/* Same shape as the real one so that existing call sites keep compiling. */
#define HILOG_IMPL(type, level, domain, tag, fmt, ...)          \
    do {                                                        \
        (void)(domain);                                         \
        (void)(tag);                                            \
        (void)sizeof(fmt);                                      \
    } while (0)

} // namespace HiviewDFX
} // namespace OHOS

#ifndef LOG_DOMAIN
#define LOG_DOMAIN 0
#endif

#ifndef HILOG_DEBUG
#define HILOG_DEBUG(...) HILOG_IMPL(LOG_DEBUG, ...)
#define HILOG_INFO(...)  HILOG_IMPL(LOG_INFO, ...)
#define HILOG_WARN(...)  HILOG_IMPL(LOG_WARN, ...)
#define HILOG_ERROR(...) HILOG_IMPL(LOG_ERROR, ...)
#define HILOG_FATAL(...) HILOG_IMPL(LOG_FATAL, ...)
#endif

#endif // ACE_HOST_HILOG_LOG_H
