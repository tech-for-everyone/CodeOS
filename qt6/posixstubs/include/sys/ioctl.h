/* CodeOS POSIX Stubs - ioctl */

#ifndef _CODEOS_SYS_IOCTL_H
#define _CODEOS_SYS_IOCTL_H

#include <stdint.h>

/* Terminal ioctl */
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[32];
    unsigned int c_ispeed;
    unsigned int c_ospeed;
};

/* ioctl request codes */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCSCTTY  0x540E
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define FIONREAD   0x541B
#define TIOCMGET   0x5415
#define TIOCMSET   0x5418
#define TIOCMBIS   0x5416
#define TIOCMBIC   0x5417

/* ioctl - stub */
static inline int ioctl(int fd, unsigned long request, ...) {
    (void)fd; (void)request;
    return -1;
}

#endif /* _CODEOS_SYS_IOCTL_H */
