#ifndef QPLATFORMDEFS_H
#define QPLATFORMDEFS_H

#include "qglobal.h"

#include <unistd.h>
#include <features.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>

// Don't define QT_USE_XOPEN_LFS_EXTENSIONS — CodeOS uses standard POSIX types
// #define QT_USE_XOPEN_LFS_EXTENSIONS
#include "../common/posix/qplatformdefs.h"

#if defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE >= 500)
#define QT_SNPRINTF             ::snprintf
#define QT_VSNPRINTF            ::vsnprintf
#endif

#endif
