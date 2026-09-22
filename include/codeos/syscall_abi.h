#ifndef CODEOS_SYSCALL_ABI_H
#define CODEOS_SYSCALL_ABI_H

/* Shared native CodeOS syscall ABI. Existing values are stable. */
#define CODEOS_SYSCALL_ABI_VERSION 1

#define SYSCALL_WRITE     0
#define SYSCALL_SLEEP     1
#define SYSCALL_EXIT      2
#define SYSCALL_GETTID    3
#define SYSCALL_OPEN      4
#define SYSCALL_READ      5
#define SYSCALL_YIELD     6
#define SYSCALL_ZIRCON_IPC 7
#define SYSCALL_FORK      8
#define SYSCALL_EXECVE    9
#define SYSCALL_WAIT      10
#define SYSCALL_GETPID    11
#define SYSCALL_GETPPID   12
#define SYSCALL_CLOSE     13
#define SYSCALL_PIPE      14
#define SYSCALL_DUP       15
#define SYSCALL_BRK       16
#define SYSCALL_MMAP      17
#define SYSCALL_MUNMAP    18
#define SYSCALL_LSEEK     19
#define SYSCALL_SBRK      20
#define SYSCALL_BINDER    21
#define SYSCALL_ASHMEM    22
#define SYSCALL_CONTAINER_CREATE  23
#define SYSCALL_CONTAINER_EXEC    24
#define SYSCALL_CONTAINER_DESTROY 25
#define SYSCALL_CONTAINER_LIST    26
#define SYSCALL_AI_QUERY          27
#define SYSCALL_FB_INFO           28
#define SYSCALL_INPUT_POLL        29
#define SYSCALL_SET_PERSONALITY   30
#define SYSCALL_AUDIO_PLAY        31
#define SYSCALL_AUDIO_STATUS      32
#define SYSCALL_PWRITE            33
#define SYSCALL_DUP2              34
#define SYSCALL_SHM               35
#define SYSCALL_GET_INFO          36
#define SYSCALL_WEB               37
#define SYSCALL_STAT              38
#define SYSCALL_CHDIR             39
#define SYSCALL_GETCWD            40
#define SYSCALL_MKDIR             41
#define SYSCALL_RMDIR             42
#define SYSCALL_UNLINK            43
#define SYSCALL_RENAME            44
#define SYSCALL_READDIR           45
#define SYSCALL_CHMOD             46
#define SYSCALL_ACCESS            47
#define SYSCALL_X11_CONNECT       48
#define SYSCALL_X11_REQUEST       49
#define SYSCALL_X11_POLL          50
#define SYSCALL_KILL              51
#define SYSCALL_VM                52
#define SYSCALL_TIME              53
#define SYSCALL_GETTIMEOFDAY      54
#define SYSCALL_CONTAINER_START   55
#define SYSCALL_AUDIO_OPEN        56
#define SYSCALL_AUDIO_WRITE       57
#define SYSCALL_AUDIO_CLOSE       58
#define SYSCALL_AUDIO_SET_VOLUME  59
#define SYSCALL_AUDIO_GET_VOLUME  60
#define SYSCALL_AUDIO_SET_STATE   61
#define SYSCALL_AUDIO_GET_POS     62
#define SYSCALL_AUDIO_GET_FREE    63
#define SYSCALL_AUDIO_SET_MASTER  64
#define SYSCALL_AUDIO_GET_MASTER  65

/* ── Socket / UDP (codeos network ABI) ──
 * sockaddr layout (identical to the kernel socket layer's sockaddr_in_t):
 *   offset 0: uint32_t s_addr   (IP as network-order value, e.g. 10.0.2.3 -> 0x0A000203)
 *   offset 4: uint16_t sin_port (port in HOST byte order, e.g. 53)
 *   total 6 useful bytes. Byte order stays host-order for port, same as C APIs. */
#define SYSCALL_SOCKET_SOCKET      66 /* socket(domain, type, protocol)      -> sockfd */
#define SYSCALL_SOCKET_CLOSE       67 /* close(sockfd)                       -> 0/-1 */
#define SYSCALL_SOCKET_BIND        68 /* bind(sockfd, addr, addrlen)         -> 0/-1 */
#define SYSCALL_SOCKET_CONNECT     69 /* connect(sockfd, addr, addrlen)      -> 0/-1 */
#define SYSCALL_SOCKET_SENDTO      70 /* sendto(fd, buf, len, flags, addr, addrlen)  -> n/-1 */
#define SYSCALL_SOCKET_RECVFROM    71 /* recvfrom(fd, buf, len, flags, addr, addrlen*) -> n/-1 */
#define SYSCALL_SOCKET_SEND        72 /* send(fd, buf, len, flags)           -> n/-1 */
#define SYSCALL_SOCKET_RECV        73 /* recv(fd, buf, len, flags)           -> n/-1 */
#define SYSCALL_SOCKET_GETSOCKNAME 74 /* getsockname(fd, addr, addrlen*)     -> 0/-1 */
#define SYSCALL_SOCKET_GETPEERNAME 75
#define SYSCALL_SOCKET_SETSOCKOPT  76 /* setsockopt(fd, level, opt, val, len) -> 0/-1 */
#define SYSCALL_SOCKET_GETSOCKOPT  77 /* getsockopt(fd, level, opt, val, len*) -> 0/-1 */

#define CODEOS_SYSCALL_COUNT 78

#endif
