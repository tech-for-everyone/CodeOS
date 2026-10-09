#ifndef _COMPAT_CTYPE_H
#define _COMPAT_CTYPE_H 1

/* glibc x86_64 bits/ctype.h mask bits, needed by libstdc++ bits/ctype_base.h */

#define _ISupper 256
#define _ISlower 512
#define _ISalpha 1024
#define _ISdigit 2048
#define _ISxdigit 4096
#define _ISspace 8192
#define _ISprint 16384
#define _ISgraph 32768
#define _ISblank 1
#define _IScntrl 2
#define _ISpunct 4
#define _ISalnum 3072

#endif
