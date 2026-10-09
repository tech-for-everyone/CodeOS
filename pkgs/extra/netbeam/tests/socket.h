/*
 * tests/socket.h — the CodeOS userspace socket.h, as seen by the host test.
 *
 * netbeam.c does `#include "socket.h"`, which resolves to the *real*
 * kernel/userspace/include/socket.h when it is compiled as a CodeOS ELF.
 * Those headers are full of `int $0x80` asm stubs, so the host test cannot
 * use them; this file exists purely to sit on the include path instead and
 * forward to netbeam_shim.h, the POSIX model of the same ABI.
 *
 * The guard matches the real header's, so if this ever shadowed the genuine
 * one in the guest build it would be a no-op rather than a silent
 * substitution.
 */

#ifndef COS_SOCKET_H
#define COS_SOCKET_H

#include "netbeam_shim.h"

#endif /* COS_SOCKET_H */