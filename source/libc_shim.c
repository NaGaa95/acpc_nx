/* Bionic-compatible libc wrappers for Unity and IL2CPP.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <malloc.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include <switch.h>
#include <EGL/egl.h>     /* eglGetProcAddress: resolve the full GLES API for dlsym */

#include "config.h"
#include "util.h"
#include "error.h"
#include "imports.h"   /* dynlib_find_export (dlsym shim lookup) */
#include "so_util.h"
#include "libc_shim.h"
#include "android_native_unity.h"

/* Only socket_fake descriptors enter Horizon BSD calls. */
#define NX_TRACKED_NET_FDS 1024
static uint8_t g_tracked_net_fd[NX_TRACKED_NET_FDS];
static uint8_t g_nonblock_net_fd[NX_TRACKED_NET_FDS];
static int nx_net_fd_is_tracked(int fd) {
  return fd >= 0 && fd < NX_TRACKED_NET_FDS && g_tracked_net_fd[fd];
}
static void nx_net_fd_set_tracked(int fd, int tracked) {
  if (fd >= 0 && fd < NX_TRACKED_NET_FDS) {
    g_tracked_net_fd[fd] = tracked ? 1 : 0;
    if (!tracked) {
      g_nonblock_net_fd[fd] = 0;
    }
  }
}

/* Android/Bionic uses Linux errno values, while libnx/newlib uses BSD-derived
 * values for most socket errors. Translate errors before returning to the
 * Android binaries; in particular, newlib EINPROGRESS=119 is Bionic 115. */
static int nx_net_errno_to_bionic(int value) {
  switch (value) {
    case ENOTSOCK:       return 88;
    case EDESTADDRREQ:   return 89;
    case EMSGSIZE:       return 90;
    case EPROTOTYPE:     return 91;
    case ENOPROTOOPT:    return 92;
    case EPROTONOSUPPORT:return 93;
    case EAFNOSUPPORT:   return 97;
    case EADDRINUSE:     return 98;
    case EADDRNOTAVAIL:  return 99;
    case ENETDOWN:       return 100;
    case ENETUNREACH:    return 101;
    case ENETRESET:      return 102;
    case ECONNABORTED:   return 103;
    case EISCONN:        return 106;
    case ENOTCONN:       return 107;
    case ETIMEDOUT:      return 110;
    case EHOSTDOWN:      return 112;
    case EHOSTUNREACH:   return 113;
    case EALREADY:       return 114;
    case EINPROGRESS:    return 115;
    case ENOTSUP:        return 95;
    default:             return value;
  }
}

static int nx_net_int_result(int result) {
  if (result < 0) errno = nx_net_errno_to_bionic(errno);
  return result;
}

static long nx_net_long_result(long result) {
  if (result < 0) errno = nx_net_errno_to_bionic(errno);
  return result;
}

static int nx_net_fd_apply_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0) return nx_net_int_result(-1);
  if (g_nonblock_net_fd[fd]) flags |= O_NONBLOCK;
  else flags &= ~O_NONBLOCK;
  return nx_net_int_result(fcntl(fd, F_SETFL, flags));
}

/* Fortify wrappers ignore object-size arguments. */

void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) { (void)dstlen; return memcpy(dst, src, n); }
void *__memmove_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) { (void)dstlen; return memmove(dst, src, n); }
void *__memset_chk_fake(void *dst, int c, size_t n, size_t dstlen) { (void)dstlen; return memset(dst, c, n); }
char *__strcat_chk_fake(char *dst, const char *src, size_t dstlen) { (void)dstlen; return strcat(dst, src); }
char *__strchr_chk_fake(const char *s, int c, size_t slen) { (void)slen; return strchr(s, c); }
char *__strcpy_chk_fake(char *dst, const char *src, size_t dstlen) { (void)dstlen; return strcpy(dst, src); }
size_t __strlen_chk_fake(const char *s, size_t slen) { (void)slen; return strlen(s); }
char *__strncat_chk_fake(char *dst, const char *src, size_t n, size_t dstlen) { (void)dstlen; return strncat(dst, src, n); }
char *__strncpy_chk_fake(char *dst, const char *src, size_t n, size_t dstlen) { (void)dstlen; return strncpy(dst, src, n); }
char *__strncpy_chk2_fake(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen) { (void)dstlen; (void)srclen; return strncpy(dst, src, n); }
char *__strrchr_chk_fake(const char *s, int c, size_t slen) { (void)slen; return strrchr(s, c); }
int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va) { (void)flag; (void)slen; return vsnprintf(s, maxlen, fmt, va); }
int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va) { (void)flag; (void)slen; return vsprintf(s, fmt, va); }

int __snprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, ...) {
  (void)flag; (void)slen;
  va_list va; va_start(va, fmt);
  int r = vsnprintf(s, maxlen, fmt, va);
  va_end(va);
  return r;
}
int __sprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, ...) {
  (void)flag; (void)slen;
  va_list va; va_start(va, fmt);
  int r = vsprintf(s, fmt, va);
  va_end(va);
  return r;
}

int   __open_2_fake(const char *path, int flags) { return open_fake(path, flags); }
long  __read_chk_fake(int fd, void *buf, size_t count, size_t buflen) { (void)buflen; return read(fd, buf, count); }
long  __pread_chk_fake(int fd, void *buf, size_t count, long off, size_t buflen) {
  (void)buflen;
  return pread_fake(fd, buf, count, off);
}
void  __FD_SET_chk_fake(int fd, void *set, size_t setlen) { (void)setlen; if (set && fd >= 0 && fd < 1024) ((unsigned long *)set)[fd / (8 * sizeof(long))] |= (1ul << (fd % (8 * sizeof(long)))); }
int   __FD_ISSET_chk_fake(int fd, const void *set, size_t setlen) { (void)setlen; if (set && fd >= 0 && fd < 1024) return (((const unsigned long *)set)[fd / (8 * sizeof(long))] >> (fd % (8 * sizeof(long)))) & 1; return 0; }

/* Android system properties used by Unity. */
int __system_property_get_fake(const char *name, char *value) {
  if (!value) return 0;
  const char *v = "";
  if (name) {
    if      (!strcmp(name, "ro.build.version.sdk"))        v = "33";
    else if (!strcmp(name, "ro.build.version.release"))    v = "13";
    else if (!strcmp(name, "ro.build.version.codename"))   v = "REL";
    else if (!strcmp(name, "ro.product.cpu.abi"))          v = "arm64-v8a";
    else if (!strcmp(name, "ro.product.cpu.abilist"))      v = "arm64-v8a";
    else if (!strcmp(name, "ro.product.cpu.abilist64"))    v = "arm64-v8a";
    else if (!strcmp(name, "ro.product.cpu.abi2"))         v = "";
    else if (!strcmp(name, "ro.product.model"))            v = "Switch";
    else if (!strcmp(name, "ro.product.manufacturer"))     v = "Nintendo";
    else if (!strcmp(name, "ro.product.brand"))            v = "Nintendo";
    else if (!strcmp(name, "ro.product.name"))             v = "Switch";
    else if (!strcmp(name, "ro.product.device"))           v = "Switch";
    else if (!strcmp(name, "ro.product.board"))            v = "nx";
    else if (!strcmp(name, "ro.hardware"))                 v = "nx";
    else if (!strcmp(name, "ro.board.platform"))           v = "nx";
    else if (!strcmp(name, "ro.build.fingerprint"))        v = "Nintendo/Switch/Switch:13/REL/51472:user/release-keys";
    else if (!strcmp(name, "ro.build.characteristics"))    v = "default";
    else if (!strcmp(name, "ro.build.type"))               v = "user";
    else if (!strcmp(name, "ro.build.tags"))               v = "release-keys";
    else if (!strcmp(name, "ro.debuggable"))               v = "0";
    else if (!strcmp(name, "ro.secure"))                   v = "1";
    else if (!strcmp(name, "ro.kernel.qemu"))              v = "0";
    else if (!strcmp(name, "ro.opengles.version"))         v = "196610"; /* GLES 3.2 */
    else if (!strcmp(name, "dalvik.vm.heapsize"))          v = "512m";
    else if (!strcmp(name, "persist.sys.timezone"))        v = "UTC";
  }
  size_t n = strlen(v);
  if (n > 91) n = 91;            /* PROP_VALUE_MAX-1 */
  memcpy(value, v, n); value[n] = '\0';
  return (int)n;
}
unsigned long getauxval_fake(unsigned long type) { (void)type; return 0; }

int gettid_fake(void) {
  u64 tid = 1;
  if (R_SUCCEEDED(svcGetThreadId(&tid, CUR_THREAD_HANDLE)) && tid)
    return (int)(tid & 0x7fffffff);
  return 1;
}

#define ARM64_SYS_GETTID            178
#define ARM64_SYS_FUTEX             98
#define ARM64_SYS_SCHED_SETAFFINITY 122
#define ARM64_SYS_PROCESS_VM_READV  270
#define ARM64_SYS_PROCESS_VM_WRITEV 271

/* Futex waits use Horizon address arbitration. */
#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_CMD_MASK    0x7f
#define FUTEX_CLOCK_REALTIME 256

static int futex_timeout_ns(int cmd, int op, const struct timespec *to, s64 *ns) {
  if (!to) { *ns = -1; return 0; }
  if (to->tv_sec < 0 || to->tv_nsec < 0 || to->tv_nsec >= 1000000000L) {
    errno = EINVAL;
    return -1;
  }

  s64 sec = (s64)to->tv_sec;
  s64 nsec = (s64)to->tv_nsec;
  if (cmd == FUTEX_WAIT_BITSET) {
    struct timespec now;
    clockid_t clock = (op & FUTEX_CLOCK_REALTIME) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    if (clock_gettime(clock, &now) != 0) return -1;
    sec -= (s64)now.tv_sec;
    nsec -= (s64)now.tv_nsec;
    if (nsec < 0) { nsec += 1000000000LL; sec--; }
    if (sec < 0 || (sec == 0 && nsec == 0)) {
      errno = ETIMEDOUT;
      return -1;
    }
  }

  if (sec > (INT64_MAX - nsec) / 1000000000LL)
    *ns = INT64_MAX;
  else
    *ns = sec * 1000000000LL + nsec;
  return 0;
}

static long futex_result(Result rc) {
  if (R_SUCCEEDED(rc)) return 0;
  if (R_MODULE(rc) == Module_Kernel) {
    switch (R_DESCRIPTION(rc)) {
      case KernelError_TimedOut: errno = ETIMEDOUT; break;
      case KernelError_InvalidState: errno = EAGAIN; break;
      case KernelError_Cancelled: errno = EINTR; break;
      case KernelError_InvalidAddress:
      case KernelError_InvalidMemoryState:
      case KernelError_InvalidMemoryRange: errno = EFAULT; break;
      default: errno = EINVAL; break;
    }
  } else {
    errno = EINVAL;
  }
  return -1;
}

static long futex_impl(volatile int32_t *uaddr, int op, int val, const struct timespec *to) {
  const int cmd = op & FUTEX_CMD_MASK;
  if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
    s64 timeout;
    if (futex_timeout_ns(cmd, op, to, &timeout) != 0) return -1;
    Result rc = svcWaitForAddress((void *)uaddr, ArbitrationType_WaitIfEqual,
                                  (s64)val, timeout);
    return futex_result(rc);
  }
  if (cmd == FUTEX_WAKE || cmd == FUTEX_WAKE_BITSET) {
    if (val <= 0) return 0;
    Result rc = svcSignalToAddress((void *)uaddr, SignalType_Signal, 0, val);
    return R_SUCCEEDED(rc) ? val : futex_result(rc);
  }
  errno = ENOSYS;
  return -1;
}

/* newlib has no <sys/uio.h>; the kernel iovec layout is just {ptr, len}. */
struct nx_iovec { void *iov_base; size_t iov_len; };

/* Validate readable ranges for process_vm_readv. */
static int nx_addr_readable(uintptr_t addr, size_t len) {
  uintptr_t a = addr, end = addr + len;
  while (a < end) {
    MemoryInfo mi; u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) return 0;
    if (mi.type == 0) return 0;                 /* MemType_Unmapped */
    if ((mi.perm & Perm_R) == 0) return 0;      /* not readable */
    uintptr_t be = (uintptr_t)mi.addr + mi.size;
    if (be <= a) return 0;
    a = be;
  }
  return 1;
}

long syscall_fake(long number, ...) {
  switch (number) {
    case ARM64_SYS_GETTID: return gettid_fake();
    case ARM64_SYS_FUTEX: {
      va_list va; va_start(va, number);
      volatile int32_t *uaddr = va_arg(va, volatile int32_t *);
      const int op  = va_arg(va, int);
      const int val = va_arg(va, int);
      const struct timespec *to = va_arg(va, const struct timespec *);
      va_end(va);
      return futex_impl(uaddr, op, val, to);
    }
    case ARM64_SYS_SCHED_SETAFFINITY:
      return 0; // affinity hints are advisory; pretend success
    case ARM64_SYS_PROCESS_VM_READV:
    case ARM64_SYS_PROCESS_VM_WRITEV: {
      /* Validate each source range before copying. */
      va_list va; va_start(va, number);
      long pid                   = va_arg(va, long); (void)pid;
      const struct nx_iovec *liov   = va_arg(va, const struct nx_iovec *);
      unsigned long lcnt         = va_arg(va, unsigned long);
      const struct nx_iovec *riov   = va_arg(va, const struct nx_iovec *);
      unsigned long rcnt         = va_arg(va, unsigned long);
      va_end(va);
      int writing = (number == ARM64_SYS_PROCESS_VM_WRITEV);
      ssize_t total = 0;
      unsigned long li = 0, ri = 0; size_t lo = 0, ro = 0;
      while (li < lcnt && ri < rcnt) {
        char *lp = (char *)liov[li].iov_base + lo;
        char *rp = (char *)riov[ri].iov_base + ro;
        size_t lrem = liov[li].iov_len - lo, rrem = riov[ri].iov_len - ro;
        size_t n = lrem < rrem ? lrem : rrem;
        char *source = writing ? lp : rp;
        if (!nx_addr_readable((uintptr_t)source, n)) {
          if (total == 0) { errno = EFAULT; return -1; }
          return total;
        }
        if (writing) memcpy(rp, lp, n); else memcpy(lp, rp, n);
        total += (ssize_t)n; lo += n; ro += n;
        if (lo == liov[li].iov_len) { li++; lo = 0; }
        if (ro == riov[ri].iov_len) { ri++; ro = 0; }
      }
      return total;
    }
  }
  errno = ENOSYS;
  return -1;
}

void sincosf_fake(float x, float *s, float *c) { *s = sinf(x); *c = cosf(x); }
int sched_get_priority_max_fake(int policy) { (void)policy; return 0; }
int sched_get_priority_min_fake(int policy) { (void)policy; return 0; }
void android_set_abort_message_fake(const char *msg) { (void)msg; }

size_t __ctype_get_mb_cur_max_fake(void) { return 1; }
int __register_atfork_fake(void) { return 0; }
int __cxa_thread_atexit_impl_fake(void (*fn)(void *), void *arg, void *dso) { (void)fn; (void)arg; (void)dso; return 0; }

#define BIONIC_SC_PAGESIZE 39
#define BIONIC_SC_PAGE_SIZE 40
#define BIONIC_SC_NPROCESSORS_CONF 96
#define BIONIC_SC_NPROCESSORS_ONLN 97
#define BIONIC_SC_PHYS_PAGES 98

long sysconf_fake(int name) {
  switch (name) {
    case BIONIC_SC_PAGESIZE:
    case BIONIC_SC_PAGE_SIZE: return 0x1000;
    case BIONIC_SC_NPROCESSORS_CONF:
    case BIONIC_SC_NPROCESSORS_ONLN: return 3;
    /* Limit Unity's dynamic-heap region count. */
    case BIONIC_SC_PHYS_PAGES: return (512ll * 1024 * 1024) / 0x1000;
    default: return -1;
  }
}
long pathconf_fake(const char *path, int name) { (void)path; (void)name; return -1; }

/* Translate bionic open flags. */

#define LINUX_O_CREAT  0100
#define LINUX_O_EXCL   0200
#define LINUX_O_TRUNC  01000
#define LINUX_O_APPEND 02000

static int convert_open_flags(int flags) {
  int out = flags & 3;
  if (flags & LINUX_O_CREAT)  out |= O_CREAT;
  if (flags & LINUX_O_EXCL)   out |= O_EXCL;
  if (flags & LINUX_O_TRUNC)  out |= O_TRUNC;
  if (flags & LINUX_O_APPEND) out |= O_APPEND;
  return out;
}

/* Map Android asset paths and jar URIs into the staged asset tree. */
static const char *resolve_game_path(const char *path, char *resolved, size_t size) {
  if (!path) { errno = EFAULT; return NULL; }
  const char *asset_path = NULL;
  if (!strcmp(path, "/assets") || !strncmp(path, "/assets/", 8)) {
    asset_path = path;
  } else if (!strncmp(path, "jar:file:", 9)) {
    const char *bang = strstr(path + 9, "!/assets");
    if (bang) asset_path = bang + 1; /* keep the leading slash */
  } else if (!strncmp(path, "file:", 5)) {
    const char *p = strstr(path + 5, "/assets");
    if (p) asset_path = p;
  }
  if (!asset_path) return path;
  int n = snprintf(resolved, size, "%s%s", GAME_HOME, asset_path);
  if (n < 0 || (size_t)n >= size) { errno = ENAMETOOLONG; return NULL; }
  return resolved;
}

/* Create a directory below a devoptab root. */
static int safe_mkdir(const char *p) {
  if (!p || !*p) { errno = EINVAL; return -1; }
  const char *colon = strchr(p, ':');
  if (colon) {
    const char *in = colon + 1;
    while (*in == '/') in++;
    if (!*in) { errno = EEXIST; return 0; }
    if (!strchr(in, '/')) { errno = EEXIST; return 0; }
  }
  return mkdir(p, 0777);
}

/* Create missing directories below GAME_HOME. */
static void mkdir_p_dir(const char *dir) {
  if (!dir || !*dir) return;
  char tmp[512];
  if (snprintf(tmp, sizeof(tmp), "%s", dir) <= 0) return;
  size_t skip;
  const size_t glen = strlen(GAME_HOME);
  if (strncmp(tmp, GAME_HOME, glen) == 0 && (tmp[glen] == '/' || tmp[glen] == '\0')) {
    skip = glen;
  } else {
    const char *colon = strchr(tmp, ':');
    skip = colon ? (size_t)(colon + 1 - tmp) : 0;
  }
  for (char *p = tmp + skip + 1; *p; p++)
    if (*p == '/') { *p = '\0'; safe_mkdir(tmp); *p = '/'; }
  if (tmp[skip]) safe_mkdir(tmp);
}
static void mkdir_parents(const char *filepath) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", filepath);
  char *last = strrchr(tmp, '/');
  if (!last || last == tmp) return;
  *last = '\0';
  mkdir_p_dir(tmp);
}

int mkdir_fake(const char *path, unsigned mode) {
  (void)mode;
  if (!path || !*path) { errno = EINVAL; return -1; }
  mkdir_p_dir(path);
  int r = safe_mkdir(path);
  if (r != 0 && errno == EEXIST) r = 0;
  return r;
}

/* AArch64 off_t also services lseek64. */
long z_lseek(int fd, long off, int whence) {
  return lseek(fd, off, whence);
}

static const char *synthetic_proc(const char *path);  /* defined below */

/* Materialize synthetic /proc and /sys content as files. */
static int synth_proc_open(const char *path) {
  if (!path) return -1;
  if (strncmp(path, "/proc/", 6) && strncmp(path, "/sys/", 5)) return -1;
  static char buf[16384];
  int len;
  if (!strcmp(path, "/proc/self/maps") || !strcmp(path, "/proc/self/smaps")) {
    len = so_dump_maps(buf, sizeof buf);
  } else {
    const char *s = synthetic_proc(path);
    if (!s) return -1;                                   // not /proc or /sys
    len = (int)strlen(s);
    if (len > (int)sizeof buf) len = (int)sizeof buf;
    memcpy(buf, s, (size_t)len);
  }
  char safe[160]; size_t j = 0;
  for (const char *p = path; *p && j < sizeof safe - 1; p++) safe[j++] = (*p == '/') ? '_' : *p;
  safe[j] = '\0';
  char tf[256];
  snprintf(tf, sizeof tf, "%s/.synth%s", GAME_HOME, safe);
  int wfd = open(tf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (wfd >= 0) { if (write(wfd, buf, (size_t)len) < 0) { /* best effort */ } close(wfd); }
  return open(tf, O_RDONLY);
}

/* Stable synthetic inodes when fsdev reports zero. */
#define FD_INO_MAX 4096
static uint64_t g_fd_ino[FD_INO_MAX];
static uint64_t path_ino(const char *path) {
  uint64_t h = 1469598103934665603ULL;               // FNV-1a 64 offset basis
  for (const unsigned char *p = (const unsigned char *)path; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
  return h ? h : 1;                                   // 0 means "no inode" -- avoid it
}
static void fd_ino_set(int fd, const char *path) { if (fd >= 0 && fd < FD_INO_MAX) g_fd_ino[fd] = path_ino(path); }
static void fd_ino_clear(int fd) { if (fd >= 0 && fd < FD_INO_MAX) g_fd_ino[fd] = 0; }

int open_fake(const char *path, int flags, ...) {
  int mode = 0666;
  if (flags & LINUX_O_CREAT) { va_list va; va_start(va, flags); mode = va_arg(va, int); va_end(va); }
  const int cvt = convert_open_flags(flags);
  const int writing = (flags & 3) != 0 || (flags & LINUX_O_CREAT);
  if (!writing) {
    /* Materialize randomGet output for /dev/random and /dev/urandom. */
    if (!strcmp(path, "/dev/urandom") || !strcmp(path, "/dev/random")) {
      static char rbuf[65536];
      randomGet(rbuf, sizeof rbuf);
      char tf[256];
      snprintf(tf, sizeof tf, "%s/.synth_dev_random", GAME_HOME);
      int wfd = open(tf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (wfd >= 0) { if (write(wfd, rbuf, sizeof rbuf) < 0) { /* best effort */ } close(wfd); }
      int rfd = open(tf, O_RDONLY);
      fd_ino_set(rfd, path);
      return rfd;
    }
    int sfd = synth_proc_open(path);
    if (sfd >= 0) { fd_ino_set(sfd, path); return sfd; }
  }
  char resolved[512];
  const char *io_path = resolve_game_path(path, resolved, sizeof resolved);
  if (!io_path) return -1;
  int fd = open(io_path, cvt, mode);
  if (fd < 0 && writing) {
    /* Create missing save directories and retry. */
    mkdir_parents(io_path);
    fd = open(io_path, cvt, mode);
  }
  if (fd >= 0) {
    fd_ino_set(fd, io_path);
  }
  return fd;
}
int openat_fake(int dirfd, const char *path, int flags, ...) {
  (void)dirfd;
  int mode = 0666;
  if (flags & LINUX_O_CREAT) { va_list va; va_start(va, flags); mode = va_arg(va, int); va_end(va); }
  return open_fake(path, flags, mode);
}
int unlinkat_fake(int dirfd, const char *path, int flags) { (void)dirfd; (void)flags; return unlink(path); }

/* Bionic AArch64 stat layout. */

struct bionic_timespec { int64_t tv_sec; int64_t tv_nsec; };
struct bionic_stat {
  uint64_t st_dev; uint64_t st_ino; uint32_t st_mode; uint32_t st_nlink;
  uint32_t st_uid; uint32_t st_gid; uint64_t st_rdev; uint64_t __pad1;
  int64_t st_size; int32_t st_blksize; int32_t __pad2; int64_t st_blocks;
  struct bionic_timespec st_atim; struct bionic_timespec st_mtim; struct bionic_timespec st_ctim;
  uint32_t __unused4; uint32_t __unused5;
};

static void convert_stat(const struct stat *in, struct bionic_stat *out) {
  memset(out, 0, sizeof(*out));
  out->st_dev = in->st_dev; out->st_ino = in->st_ino; out->st_mode = in->st_mode;
  out->st_nlink = in->st_nlink; out->st_uid = in->st_uid; out->st_gid = in->st_gid;
  out->st_rdev = in->st_rdev; out->st_size = in->st_size; out->st_blksize = in->st_blksize;
  out->st_blocks = in->st_blocks;
  out->st_atim.tv_sec = in->st_atime; out->st_mtim.tv_sec = in->st_mtime; out->st_ctim.tv_sec = in->st_ctime;
}

int stat_fake(const char *path, struct bionic_stat *st) {
  char resolved[512];
  const char *io_path = resolve_game_path(path, resolved, sizeof resolved);
  if (!io_path) return -1;
  struct stat real; int r = stat(io_path, &real);
  if (r == 0) {
    convert_stat(&real, st);
    if (st->st_ino == 0) st->st_ino = path_ino(io_path);
  }
  return r;
}
int fstat_fake(int fd, struct bionic_stat *st) {
  struct stat real; const int r = fstat(fd, &real);
  if (r == 0) {
    convert_stat(&real, st);
    if (st->st_ino == 0) {
      uint64_t ino = (fd >= 0 && fd < FD_INO_MAX) ? g_fd_ino[fd] : 0;
      st->st_ino = ino ? ino : ((uint64_t)(fd + 1) * 2654435761ULL) | 1;
    }
  }
  return r;
}
int lstat_fake(const char *path, struct bionic_stat *st) { return stat_fake(path, st); }

int access_fake(const char *path, int mode) {
  (void)mode;
  char resolved[512];
  const char *io_path = resolve_game_path(path, resolved, sizeof resolved);
  if (!io_path) return -1;
  struct stat st;
  int r = stat(io_path, &st) == 0 ? 0 : -1;
  return r;
}

void *opendir_fake(const char *path) {
  char resolved[512];
  const char *io_path = resolve_game_path(path, resolved, sizeof resolved);
  if (!io_path) return NULL;
  return opendir(io_path);
}

/* Bionic dirent64 layout. */

struct bionic_dirent {
  uint64_t d_ino; int64_t d_off; uint16_t d_reclen; uint8_t d_type; char d_name[256];
};

void *readdir_fake(void *dirp) {
  static struct bionic_dirent out; // not thread-safe (matches bionic readdir)
  struct dirent *e = readdir((DIR *)dirp);
  if (!e) return NULL;
  memset(&out, 0, sizeof(out));
  out.d_ino = e->d_ino;
  out.d_reclen = sizeof(out);
  out.d_type = e->d_type;
  snprintf(out.d_name, sizeof(out.d_name), "%s", e->d_name);
  return &out;
}

/* Locale wrappers use the C locale. */

void *newlocale_fake(int mask, const char *locale, void *base) { (void)mask; (void)locale; (void)base; return (void *)1; }
void freelocale_fake(void *loc) { (void)loc; }
void *uselocale_fake(void *loc) { (void)loc; return (void *)1; }

#define WRAP_ISW_L(fn) int fn##_l_fake(int wc, void *loc) { (void)loc; return fn(wc); }
WRAP_ISW_L(iswalpha) WRAP_ISW_L(iswblank) WRAP_ISW_L(iswcntrl) WRAP_ISW_L(iswdigit)
WRAP_ISW_L(iswlower) WRAP_ISW_L(iswprint) WRAP_ISW_L(iswpunct) WRAP_ISW_L(iswspace)
WRAP_ISW_L(iswupper) WRAP_ISW_L(iswxdigit) WRAP_ISW_L(towlower) WRAP_ISW_L(towupper)

int strcoll_l_fake(const char *a, const char *b, void *loc) { (void)loc; return strcoll(a, b); }
size_t strxfrm_l_fake(char *dst, const char *src, size_t n, void *loc) { (void)loc; return strxfrm(dst, src, n); }
size_t strftime_l_fake(char *s, size_t max, const char *fmt, const void *tm, void *loc) { (void)loc; return strftime(s, max, fmt, (const struct tm *)tm); }
long double strtold_l_fake(const char *s, char **end, void *loc) { (void)loc; return strtold(s, end); }
long long strtoll_l_fake(const char *s, char **end, int base, void *loc) { (void)loc; return strtoll(s, end, base); }
unsigned long long strtoull_l_fake(const char *s, char **end, int base, void *loc) { (void)loc; return strtoull(s, end, base); }
int wcscoll_l_fake(const wchar_t *a, const wchar_t *b, void *loc) { (void)loc; return wcscoll(a, b); }
size_t wcsxfrm_l_fake(wchar_t *dst, const wchar_t *src, size_t n, void *loc) { (void)loc; return wcsxfrm(dst, src, n); }

size_t mbsnrtowcs_fake(wchar_t *dst, const char **src, size_t nms, size_t len, void *ps) {
  (void)ps;
  size_t i = 0; const char *s = *src;
  while (i < nms && s[i] && (!dst || i < len)) { if (dst) dst[i] = (unsigned char)s[i]; i++; }
  if (dst && i < len) { dst[i] = 0; *src = NULL; }
  return i;
}
size_t wcsnrtombs_fake(char *dst, const wchar_t **src, size_t nwc, size_t len, void *ps) {
  (void)ps;
  size_t i = 0; const wchar_t *s = *src;
  while (i < nwc && s[i] && (!dst || i < len)) { if (dst) dst[i] = (char)s[i]; i++; }
  if (dst && i < len) { dst[i] = 0; *src = NULL; }
  return i;
}

int posix_memalign_fake(void **out, size_t align, size_t size) {
  void *p = memalign(align, size);
  if (!p) return ENOMEM;
  *out = p;
  return 0;
}

/* Page-granular arena supporting Unity's partial unmaps. */
extern void  *g_mmap_arena_base;   // set by __libnx_initheap (main.c)
extern size_t g_mmap_arena_size;

#define BIONIC_MAP_FIXED     0x10
#define BIONIC_MAP_ANONYMOUS 0x20
#define MMAP_PAGE       0x1000u
#define MMAP_BIG_ALIGN  MMAP_ARENA_ALIGN
#define MMAP_BIG_THRESH ((size_t)64 * 1024 * 1024)
#define MMAP_SPARSE_COMMIT_THRESH ((size_t)1 * 1024 * 1024)
#define BIONIC_PROT_NONE 0x0
#define BIONIC_PROT_WRITE 0x2

static uint8_t *mmap_arena;    // aligned usable base (published last)
static size_t   mmap_usable;   // usable bytes
static size_t   mmap_pages;    // usable / page
static uint8_t *mmap_used;     // 1 byte/page bitmap: reserved (address space)
static Mutex    g_mmap_lock;   // zero-init == valid unlocked libnx mutex

/* Sparse ASLR arena backed on demand by reusable heap pages. */
#define OC_NOSRC 0xFFFFFFFFu
static uint8_t  *oc_base;
static size_t    oc_pages;       // window size in pages (0 => OC disabled)
static uint8_t  *oc_used;        // 1/page: address space reserved by an mmap
static uint8_t  *oc_committed;   // 1/page: aliased from the commit pool
static uint32_t *oc_srcpg;       // 1/page: pool page aliased in (OC_NOSRC = none)
static uint8_t  *oc_pool;        // commit-pool base (heap, page-aligned)
static size_t    oc_pool_pages;  // pool capacity in pages
static size_t    oc_pool_bump;   // next never-used pool page
static uint32_t *oc_pool_next;   // free-list links (per pool page)
static uint32_t  oc_pool_freehead = OC_NOSRC;  // recycled pool pages
static size_t    oc_pool_free_count;

static Result oc_map_pages(void *dst, void *src, size_t len) {
  Handle process = envGetOwnProcessHandle();
  Result rc = svcMapProcessCodeMemory(process, (u64)(uintptr_t)dst,
                                      (u64)(uintptr_t)src, (u64)len);
  if (R_SUCCEEDED(rc)) {
    rc = svcSetProcessMemoryPermission(process, (u64)(uintptr_t)dst,
                                       (u64)len, Perm_Rw);
    if (R_FAILED(rc))
      svcUnmapProcessCodeMemory(process, (u64)(uintptr_t)dst,
                               (u64)(uintptr_t)src, (u64)len);
  }
  return rc;
}

static Result oc_unmap_pages(void *dst, void *src, size_t len) {
  return svcUnmapProcessCodeMemory(envGetOwnProcessHandle(),
                                   (u64)(uintptr_t)dst,
                                   (u64)(uintptr_t)src, (u64)len);
}

/* Initialize the sparse ASLR arena. */
int oc_arena_init(void *window, size_t window_bytes, void *pool, size_t pool_bytes) {
  if (!window || !pool || !window_bytes || !pool_bytes) return 0;
  size_t wp = window_bytes / MMAP_PAGE, pp = pool_bytes / MMAP_PAGE;
  uint8_t  *u = (uint8_t *)calloc(wp, 1);
  uint8_t  *c = (uint8_t *)calloc(wp, 1);
  uint32_t *s = (uint32_t *)malloc(wp * sizeof *s);
  uint32_t *n = (uint32_t *)malloc(pp * sizeof *n);
  if (!u || !c || !s || !n) { free(u); free(c); free(s); free(n); return 0; }
  memset(s, 0xFF, wp * sizeof *s);
  mutexLock(&g_mmap_lock);
  oc_base = (uint8_t *)window; oc_pages = wp; oc_used = u; oc_committed = c;
  oc_srcpg = s; oc_pool = (uint8_t *)pool; oc_pool_pages = pp;
  oc_pool_bump = 0; oc_pool_next = n; oc_pool_freehead = OC_NOSRC;
  oc_pool_free_count = 0;
  mutexUnlock(&g_mmap_lock);
  return 1;
}

static int oc_contains(void *addr) {
  return oc_pages && (uint8_t *)addr >= oc_base &&
         (uint8_t *)addr < oc_base + oc_pages * MMAP_PAGE;
}

/* Reject unexpected mappings inside the reserved window. */
static int oc_range_occupied(size_t i, size_t need) {
  uint64_t a   = (uint64_t)(uintptr_t)(oc_base + i * MMAP_PAGE);
  uint64_t end = a + (uint64_t)need * MMAP_PAGE;
  int occ = 0;
  while (a < end) {
    MemoryInfo mi; u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) { occ = 1; break; }
    uint64_t span_end = mi.addr + mi.size;
    if (span_end <= a) { occ = 1; break; }
    if (mi.type != MemType_Unmapped) {
      occ = 1;
      uint64_t s = mi.addr > (uint64_t)(uintptr_t)oc_base ? mi.addr
                                                          : (uint64_t)(uintptr_t)oc_base;
      size_t p0 = (size_t)((s - (uint64_t)(uintptr_t)oc_base) / MMAP_PAGE);
      size_t p1 = (size_t)((span_end - (uint64_t)(uintptr_t)oc_base + MMAP_PAGE - 1) / MMAP_PAGE);
      for (size_t k = p0; k < p1 && k < oc_pages; k++) oc_used[k] = 1;
    }
    a = span_end;
  }
  return occ;
}

/* Reserve aligned address space in the sparse arena. */
static void *oc_alloc_locked(size_t len, size_t *got) {
  *got = 0;
  if (!oc_pages) return NULL;
  size_t need = (len + MMAP_PAGE - 1) / MMAP_PAGE; if (!need) need = 1;
  if (len < MMAP_BIG_THRESH) {
    for (size_t i = 0; i + need <= oc_pages; ) {
      size_t run = 0;
      while (run < need && !oc_used[i + run]) run++;
      if (run == need) {
        if (oc_range_occupied(i, need)) { i += need; continue; }
        for (size_t k = 0; k < need; k++) oc_used[i + k] = 1;
        *got = need * MMAP_PAGE;
        return oc_base + i * MMAP_PAGE;
      }
      i += run + 1;
    }
    return NULL;
  }
  const size_t step = MMAP_BIG_ALIGN / MMAP_PAGE;
  size_t kept = need > step ? need - step + 1 : need;
  for (size_t i = 0; i + need <= oc_pages; i += step) {        // pass 1: full over-map fits
    size_t run = 0; while (run < need && !oc_used[i + run]) run++;
    if (run == need) {
      if (oc_range_occupied(i, need)) continue;
      for (size_t k = 0; k < need; k++) oc_used[i + k] = 1;
      *got = need * MMAP_PAGE; return oc_base + i * MMAP_PAGE;
    }
  }
  for (size_t i = 0; i < oc_pages; i += step) {
    if (i + need <= oc_pages) continue;
    size_t avail = oc_pages - i; if (avail < kept) continue;
    size_t run = 0; while (run < avail && !oc_used[i + run]) run++;
    if (run == avail) {
      if (oc_range_occupied(i, avail)) continue;
      for (size_t k = 0; k < avail; k++) oc_used[i + k] = 1;
      *got = avail * MMAP_PAGE; return oc_base + i * MMAP_PAGE;
    }
  }
  return NULL;
}

static void __attribute__((noreturn)) oc_pool_exhausted(size_t pages) {
  size_t active = oc_pool_bump - oc_pool_free_count;
  fatal_error("Out of memory: mmap backing exhausted "
              "(%zu/%zu MB active, %zu MB requested).",
              (active * MMAP_PAGE) >> 20,
              (oc_pool_pages * MMAP_PAGE) >> 20,
              (pages * MMAP_PAGE + ((size_t)1 << 20) - 1) >> 20);
}

/* Commit pool pages into a sparse reservation. Caller holds g_mmap_lock. */
static void oc_commit_locked(void *addr, size_t len) {
  if ((uint8_t *)addr < oc_base) return;
  size_t first = ((uint8_t *)addr - oc_base) / MMAP_PAGE;
  size_t cnt   = (len + MMAP_PAGE - 1) / MMAP_PAGE;
  if (first >= oc_pages) return;
  if (first + cnt > oc_pages) cnt = oc_pages - first;
  size_t i = 0;
  while (i < cnt) {
    if (oc_committed[first + i]) { i++; continue; }
    size_t run = 0;
    while (i + run < cnt && !oc_committed[first + i + run]) run++;
    size_t done = 0;
    while (done < run) {
      size_t chunk, src0;
      int from_bump = oc_pool_bump < oc_pool_pages;
      if (from_bump) {                                 // fresh contiguous chunk
        chunk = run - done;
        if (oc_pool_bump + chunk > oc_pool_pages) chunk = oc_pool_pages - oc_pool_bump;
        src0 = oc_pool_bump;
      } else if (oc_pool_freehead != OC_NOSRC) {
        chunk = 1; src0 = oc_pool_freehead;
      } else {
        oc_pool_exhausted(run - done);
      }
      void *dst = oc_base + (first + i + done) * MMAP_PAGE;
      void *src = oc_pool + src0 * MMAP_PAGE;
      Result rc = oc_map_pages(dst, src, chunk * MMAP_PAGE);
      if (R_FAILED(rc)) {
        for (size_t k = 0; k < chunk; k++) {
          size_t page = first + i + done + k;
          void *page_dst = oc_base + page * MMAP_PAGE;
          MemoryInfo dst_info; u32 page_info;
          Result query_rc = svcQueryMemory(&dst_info, &page_info,
                                           (u64)(uintptr_t)page_dst);
          if (R_SUCCEEDED(query_rc) && dst_info.type != MemType_Unmapped) {
            fatal_error("Game heap collided with system memory at %p "
                        "(type=0x%x perm=0x%x).",
                        page_dst, dst_info.type, dst_info.perm);
          }

          int page_from_bump = oc_pool_bump < oc_pool_pages;
          size_t page_src;
          if (page_from_bump) page_src = oc_pool_bump;
          else if (oc_pool_freehead != OC_NOSRC) page_src = oc_pool_freehead;
          else oc_pool_exhausted(1);

          void *page_src_addr = oc_pool + page_src * MMAP_PAGE;
          Result page_rc = oc_map_pages(page_dst, page_src_addr, MMAP_PAGE);
          if (R_FAILED(page_rc)) {
            MemoryInfo src_info; u32 src_page_info;
            memset(&src_info, 0, sizeof src_info);
            svcQueryMemory(&src_info, &src_page_info,
                           (u64)(uintptr_t)page_src_addr);
            fatal_error("Game heap page map failed at %p from %p "
                        "(rc=0x%x dst=0x%x src=0x%x attr=0x%x).",
                        page_dst, page_src_addr, page_rc,
                        R_SUCCEEDED(query_rc) ? dst_info.type : UINT32_MAX,
                        src_info.type, src_info.attr);
          }
          if (page_from_bump) {
            oc_pool_bump++;
          } else {
            oc_pool_freehead = oc_pool_next[page_src];
            oc_pool_free_count--;
          }
          memset(page_dst, 0, MMAP_PAGE);
          oc_committed[page] = 1;
          oc_srcpg[page] = (uint32_t)page_src;
        }
        done += chunk;
        continue;
      }
      if (from_bump) {
        oc_pool_bump += chunk;
      } else {
        oc_pool_freehead = oc_pool_next[src0];
        oc_pool_free_count--;
      }
      memset(dst, 0, chunk * MMAP_PAGE);   // committed anon must read as zero
      for (size_t k = 0; k < chunk; k++) {
        oc_committed[first + i + done + k] = 1;
        oc_srcpg[first + i + done + k] = (uint32_t)(src0 + k);
      }
      done += chunk;
    }
    i += run;
  }
}

/* Decommit full pages and recycle their backing. */
static void oc_decommit_locked(void *addr, size_t len) {
  if (!oc_pages || (uint8_t *)addr < oc_base) return;
  size_t off   = (size_t)((uint8_t *)addr - oc_base);
  size_t first = (off + MMAP_PAGE - 1) / MMAP_PAGE;        // partial head page stays
  size_t lastx = (off + len) / MMAP_PAGE;
  if (lastx > oc_pages) lastx = oc_pages;
  size_t i = first;
  while (i < lastx) {
    if (!oc_committed[i]) { i++; continue; }
    size_t run = 1;                                        // batch contiguous dst+src
    while (i + run < lastx && oc_committed[i + run] &&
           oc_srcpg[i + run] == oc_srcpg[i] + run) run++;
    void *dst = oc_base + i * MMAP_PAGE;
    void *src = oc_pool + (size_t)oc_srcpg[i] * MMAP_PAGE;
    if (R_SUCCEEDED(oc_unmap_pages(dst, src, run * MMAP_PAGE))) {
      for (size_t k = 0; k < run; k++) {
        uint32_t s = oc_srcpg[i + k];
        oc_pool_next[s] = oc_pool_freehead; oc_pool_freehead = s;
        oc_pool_free_count++;
        oc_committed[i + k] = 0; oc_srcpg[i + k] = OC_NOSRC;
      }
    }
    i += run;
  }
}

/* Release a sparse-arena reservation. */
static void oc_free_locked(void *addr, size_t len) {
  if ((uint8_t *)addr < oc_base) return;
  size_t first = ((uint8_t *)addr - oc_base) / MMAP_PAGE;
  size_t cnt   = (len + MMAP_PAGE - 1) / MMAP_PAGE;
  if (first >= oc_pages) return;
  if (first + cnt > oc_pages) cnt = oc_pages - first;
  oc_decommit_locked(addr, cnt * MMAP_PAGE);
  for (size_t i = 0; i < cnt; i++)
    if (!oc_committed[first + i]) oc_used[first + i] = 0;
}

static size_t oc_pool_available_locked(void) {
  return oc_pool_pages - oc_pool_bump + oc_pool_free_count;
}

static void mmap_arena_init_locked(void) {
  if (mmap_arena) return;
  if (!g_mmap_arena_base || !g_mmap_arena_size)
    fatal_error("mmap arena is unavailable; launch through full title override");
  uint8_t *base = (uint8_t *)g_mmap_arena_base;
  size_t usable = g_mmap_arena_size;
  size_t pages  = usable / MMAP_PAGE;
  uint8_t *used = (uint8_t *)calloc(pages, 1);
  if (!used) fatal_error("mmap bitmap alloc failed");
  mmap_usable = usable; mmap_pages = pages; mmap_used = used;
  mmap_arena  = base;
}

/* Reserve an aligned arena run. */
static void *mmap_arena_alloc_locked(size_t len, size_t *got) {
  size_t need = (len + MMAP_PAGE - 1) / MMAP_PAGE;
  if (!need) need = 1;
  if (len >= MMAP_BIG_THRESH) {
    const size_t step = MMAP_BIG_ALIGN / MMAP_PAGE;
    size_t kept = need > step ? need - step + 1 : need;
    /* Full over-map pass. */
    for (size_t i = 0; i + need <= mmap_pages; i += step) {
      size_t run = 0;
      while (run < need && !mmap_used[i + run]) run++;
      if (run == need) {
        for (size_t k = 0; k < need; k++) mmap_used[i + k] = 1;
        *got = need * MMAP_PAGE;
        return mmap_arena + i * MMAP_PAGE;
      }
    }
    /* Tail pass keeps only the aligned block. */
    for (size_t i = 0; i < mmap_pages; i += step) {
      if (i + need <= mmap_pages) continue;          // handled by pass 1
      size_t avail = mmap_pages - i;
      if (avail < kept) continue;                    // kept block wouldn't fit
      size_t run = 0;
      while (run < avail && !mmap_used[i + run]) run++;
      if (run == avail) {
        for (size_t k = 0; k < avail; k++) mmap_used[i + k] = 1;
        *got = avail * MMAP_PAGE;
        return mmap_arena + i * MMAP_PAGE;
      }
    }
  } else {
    for (size_t i = 0; i + need <= mmap_pages; ) {
      size_t run = 0;
      while (run < need && !mmap_used[i + run]) run++;
      if (run == need) {
        for (size_t k = 0; k < need; k++) mmap_used[i + k] = 1;
        *got = need * MMAP_PAGE;
        return mmap_arena + i * MMAP_PAGE;
      }
      i += run + 1;
    }
  }
  *got = 0;
  return NULL;
}

static void mmap_arena_free(void *addr, size_t len) {
  if (!mmap_arena || (uint8_t *)addr < mmap_arena) return;
  size_t off = (uint8_t *)addr - mmap_arena;
  if (off >= mmap_usable) return;
  size_t first = off / MMAP_PAGE;
  size_t cnt   = (len + MMAP_PAGE - 1) / MMAP_PAGE;
  mutexLock(&g_mmap_lock);
  for (size_t k = 0; k < cnt && first + k < mmap_pages; k++)
    mmap_used[first + k] = 0;
  mutexUnlock(&g_mmap_lock);
}

/* Heap-backed mappings used when the fixed arenas are full. */
#define MMAP_FALLBACK_MAX 4096
static struct { void *ptr; size_t len; } g_fb[MMAP_FALLBACK_MAX];
static int   g_fb_n = 0;
static Mutex g_fb_lock;

static void mmap_fill_file(void *dst, size_t length, int fd, long offset) {
  size_t got = 0;
  if (fd >= 0) {
    while (got < length) {
      long r = pread_fake(fd, (char *)dst + got, length - got,
                          offset + (long)got);
      if (r <= 0) break;
      if ((size_t)r > length - got) r = (long)(length - got);
      got += (size_t)r;
    }
  }
  if (got < length) memset((char *)dst + got, 0, length - got);
}

static void *mmap_fallback(size_t length, int flags, int fd, long offset) {
  /* Unity dynamic heaps require MMAP_BIG_ALIGN alignment. */
  size_t align = (length >= MMAP_BIG_THRESH && (flags & BIONIC_MAP_ANONYMOUS))
                   ? MMAP_BIG_ALIGN : MMAP_PAGE;
  void *q = memalign(align, length);
  if (!q) return NULL;
  if (flags & BIONIC_MAP_ANONYMOUS) {
    memset(q, 0, length);
  } else {
    mmap_fill_file(q, length, fd, offset);
  }
  int tracked = 0;
  mutexLock(&g_fb_lock);
  if (g_fb_n < MMAP_FALLBACK_MAX) {
    g_fb[g_fb_n].ptr = q;
    g_fb[g_fb_n].len = length;
    g_fb_n++;
    tracked = 1;
  }
  mutexUnlock(&g_fb_lock);
  if (!tracked) {
    free(q);
    errno = ENOMEM;
    return NULL;
  }
  return q;
}

/* Keep large AssetBundle mappings out of the ordinary C heap. The initial
 * content install can have more than MMAP_FIXED_BYTES mapped at once; using
 * malloc for that overflow starves JNI and other small native allocations.
 * The sparse pool is page-backed, recycled by munmap, and already reserved
 * for Unity's large mappings. */
static void *mmap_sparse_file(size_t length, int fd, long offset) {
  if (!oc_pages || fd < 0 || length < MMAP_SPARSE_COMMIT_THRESH) return NULL;

  void *p = NULL;
  size_t reserved = 0;
  const size_t need = (length + MMAP_PAGE - 1) / MMAP_PAGE;
  mutexLock(&g_mmap_lock);
  if (need <= oc_pool_available_locked()) {
    p = oc_alloc_locked(length, &reserved);
    if (p && reserved < length) {
      oc_free_locked(p, reserved);
      p = NULL;
    }
    if (p) oc_commit_locked(p, length);
  }
  mutexUnlock(&g_mmap_lock);

  if (p) mmap_fill_file(p, length, fd, offset);
  return p;
}

/* Free a tracked heap-backed mapping. */
static int mmap_fallback_free(void *addr) {
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_fb_n; i++) {
    if (g_fb[i].ptr == addr) {
      free(addr);
      g_fb[i] = g_fb[--g_fb_n];
      mutexUnlock(&g_fb_lock);
      return 1;
    }
  }
  mutexUnlock(&g_fb_lock);
  return 0;
}

/* MAP_FIXED must replace Boehm reservations in place and return the same address. */
static int mmap_remap_fixed_existing(void *addr, size_t length, int prot, int flags) {
  if (!(flags & BIONIC_MAP_FIXED) || !(flags & BIONIC_MAP_ANONYMOUS) || !addr)
    return 0;
  uintptr_t a = (uintptr_t)addr;
  if ((a & (MMAP_PAGE - 1)) || length > UINTPTR_MAX - a) return 0;
  size_t pages = (length + MMAP_PAGE - 1) / MMAP_PAGE;
  if (!pages) pages = 1;

  if (oc_contains(addr)) {
    size_t first = ((uint8_t *)addr - oc_base) / MMAP_PAGE;
    if (first + pages <= oc_pages) {
      int valid = 1;
      mutexLock(&g_mmap_lock);
      for (size_t i = 0; i < pages; i++)
        if (!oc_used[first + i]) { valid = 0; break; }
      if (valid) {
        if (prot == BIONIC_PROT_NONE) oc_decommit_locked(addr, length);
        else                          oc_commit_locked(addr, length);
      }
      mutexUnlock(&g_mmap_lock);
      if (valid) return 1;
    }
  }

  if (mmap_arena && (uint8_t *)addr >= mmap_arena) {
    size_t off = (size_t)((uint8_t *)addr - mmap_arena);
    size_t first = off / MMAP_PAGE;
    if (off < mmap_usable && first + pages <= mmap_pages) {
      int valid = 1;
      mutexLock(&g_mmap_lock);
      for (size_t i = 0; i < pages; i++)
        if (!mmap_used[first + i]) { valid = 0; break; }
      if (valid) memset(addr, 0, pages * MMAP_PAGE);
      mutexUnlock(&g_mmap_lock);
      if (valid) return 1;
    }
  }

  /* Heap-backed mappings stay in place and are cleared. */
  int fallback = 0;
  mutexLock(&g_fb_lock);
  for (int i = 0; i < g_fb_n; i++) {
    uintptr_t b = (uintptr_t)g_fb[i].ptr;
    if (a >= b && a + pages * MMAP_PAGE <= b + g_fb[i].len) {
      fallback = 1;
      break;
    }
  }
  mutexUnlock(&g_fb_lock);
  if (fallback) {
    memset(addr, 0, pages * MMAP_PAGE);
    return 1;
  }
  return 0;
}

void *mmap_fake(void *addr, size_t length, int prot, int flags, int fd, long offset) {
  if (length == 0) length = 1;

  if (mmap_remap_fixed_existing(addr, length, prot, flags)) return addr;
  if ((flags & BIONIC_MAP_FIXED) && (flags & BIONIC_MAP_ANONYMOUS)) {
    errno = ENOMEM;
    return (void *)-1;
  }

  /* Place overcommitted heaps and large direct mappings in sparse ASLR. */
  int anonymous = (flags & BIONIC_MAP_ANONYMOUS) != 0;
  int sparse_reservation = anonymous && prot == BIONIC_PROT_NONE &&
                           length >= MMAP_SPARSE_COMMIT_THRESH;
  int sparse_committed = anonymous && prot != BIONIC_PROT_NONE &&
                         length >= MMAP_SPARSE_COMMIT_THRESH;
  if (oc_pages && (sparse_reservation || sparse_committed)) {
    size_t ocres = 0;
    void *op = NULL;
    mutexLock(&g_mmap_lock);
    size_t need = (length + MMAP_PAGE - 1) / MMAP_PAGE;
    if (sparse_reservation || need <= oc_pool_available_locked()) {
      op = oc_alloc_locked(length, &ocres);
      if (op && sparse_committed) oc_commit_locked(op, length);
    }
    mutexUnlock(&g_mmap_lock);
    if (op) return op;
  }

  size_t reserved = 0;
  mutexLock(&g_mmap_lock);
  mmap_arena_init_locked();
  void *p = mmap_arena_alloc_locked(length, &reserved);
  mutexUnlock(&g_mmap_lock);
  /* File mappings require the full requested reservation. */
  if (p && !(flags & BIONIC_MAP_ANONYMOUS) && fd >= 0 && reserved < length) {
    mmap_arena_free(p, length);
    p = NULL;
  }
  if (!p) {
    if (!(flags & BIONIC_MAP_ANONYMOUS) && fd >= 0) {
      void *q = mmap_sparse_file(length, fd, offset);
      if (q) return q;
    }
    /* Use a tracked heap mapping when both fixed arenas are full. Keeping old
     * file mappings pinned here leaks memory and can return stale bundle data. */
    void *q = mmap_fallback(length, flags, fd, offset);
    if (q) return q;
    errno = ENOMEM; return (void *)-1;
  }

  /* Tail over-maps may reserve less than the requested length. */
  size_t fill = length < reserved ? length : reserved;

  if (flags & BIONIC_MAP_ANONYMOUS) {
    memset(p, 0, fill);
  } else {
    /* Load file-backed mappings into memory. */
    mmap_fill_file(p, fill, fd, offset);
  }
  return p;
}

int munmap_fake(void *addr, size_t length) {
  if (mmap_fallback_free(addr)) return 0;
  if (oc_contains(addr)) {
    mutexLock(&g_mmap_lock);
    oc_free_locked(addr, length);
    mutexUnlock(&g_mmap_lock);
    return 0;
  }
  mmap_arena_free(addr, length);
  return 0;
}

int mprotect_fake(void *addr, size_t len, int prot) {
  if (oc_contains(addr)) {
    mutexLock(&g_mmap_lock);
    if (prot == BIONIC_PROT_NONE) oc_decommit_locked(addr, len);
    else                          oc_commit_locked(addr, len);
    mutexUnlock(&g_mmap_lock);
    return 0;
  }
  return 0;
}
int madvise_fake(void *addr, size_t len, int advice) {
  (void)addr; (void)len; (void)advice;
  return 0;
}

char *realpath_fake(const char *path, char *resolved) {
  if (!path) return NULL;
  if (!resolved) resolved = malloc(0x1000);
  strcpy(resolved, path);
  return resolved;
}
int strerror_r_fake(int err, char *buf, size_t len) { snprintf(buf, len, "%s", strerror(err)); return 0; }
/* Report conservative writable capacity in bionic filesystem layouts. */
struct bionic_statvfs {
  uint64_t f_bsize, f_frsize, f_blocks, f_bfree, f_bavail;
  uint64_t f_files, f_ffree, f_favail, f_fsid, f_flag, f_namemax;
  uint32_t reserved[6];
};
int statvfs_fake(const char *path, void *buf) {
  (void)path;
  if (!buf) { errno = EFAULT; return -1; }
  struct bionic_statvfs *s = buf;
  memset(s, 0, sizeof(*s));
  s->f_bsize = s->f_frsize = 4096;
  s->f_blocks = (UINT64_C(8) << 30) / 4096;
  s->f_bfree = s->f_bavail = (UINT64_C(4) << 30) / 4096;
  s->f_files = UINT64_C(1) << 20; s->f_ffree = s->f_favail = s->f_files / 2;
  s->f_namemax = 255;
  return 0;
}
struct bionic_statfs {
  int64_t f_type, f_bsize;
  uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
  int32_t f_fsid[2];
  int64_t f_namelen, f_frsize, f_flags, f_spare[4];
};
int statfs_fake(const char *path, void *buf) {
  (void)path;
  if (!buf) { errno = EFAULT; return -1; }
  struct bionic_statfs *s = buf;
  memset(s, 0, sizeof(*s));
  s->f_bsize = s->f_frsize = 4096;
  s->f_blocks = (UINT64_C(8) << 30) / 4096;
  s->f_bfree = s->f_bavail = (UINT64_C(4) << 30) / 4096;
  s->f_files = UINT64_C(1) << 20; s->f_ffree = s->f_files / 2;
  s->f_namelen = 255;
  return 0;
}

/* Synthetic memory and CPU information for Unity sizing. */
static const char *synthetic_proc(const char *path) {
  if (!path) return NULL;
  if (!strcmp(path, "/proc/meminfo"))
    return "MemTotal:        524288 kB\n"
           "MemFree:         393216 kB\n"
           "MemAvailable:    393216 kB\n"
           "Buffers:              0 kB\n"
           "Cached:               0 kB\n"
           "SwapTotal:            0 kB\n"
           "SwapFree:             0 kB\n";
  if (!strcmp(path, "/proc/cpuinfo"))
    return "processor\t: 0\nprocessor\t: 1\nprocessor\t: 2\n"
           "Features\t: fp asimd aes pmull sha1 sha2 crc32\n"
           "CPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\n"
           "CPU part\t: 0xd07\nCPU revision\t: 1\n";
  if (strstr(path, "cpu_capacity")) return "1024\n";
  if (strstr(path, "cpuinfo_max_freq") || strstr(path, "scaling_max_freq")) return "1785000\n";
  if (strstr(path, "cpuinfo_min_freq") || strstr(path, "scaling_min_freq")) return "1020000\n";
  if (strstr(path, "/cpu/possible") || strstr(path, "/cpu/present") || strstr(path, "/cpu/online"))
    return "0-2\n";
  if (!strncmp(path, "/proc/", 6) || !strncmp(path, "/sys/", 5)) return ""; // empty for the rest
  return NULL;
}

/* Buffer large archive reads. */
FILE *fopen_fake(const char *path, const char *mode) {
  const char *synth = synthetic_proc(path);
  if (synth) {
    size_t n = strlen(synth);
    return fmemopen((void *)strdup(synth), n ? n : 1, "r");
  }
  char resolved[512];
  const char *io_path = resolve_game_path(path, resolved, sizeof resolved);
  if (!io_path) return NULL;
  const int writing = strpbrk(mode, "wa+") != NULL;
  FILE *f = fopen(io_path, mode);
  if (!f && writing) {            // save file: create the subdir and retry
    mkdir_parents(io_path);
    f = fopen(io_path, mode);
  }
  if (!f)
    return NULL;
  if (strchr(mode, 'r')) setvbuf(f, NULL, _IOFBF, 256 * 1024);
  return f;
}

/* Bionic __sF wrappers. */

uint8_t fake_sF[3][0x100]; // referenced by imports.c (__sF / std{in,out,err})

static int is_fake_file(const void *f) {
  const uint8_t *p = f;
  const uint8_t *base = (const uint8_t *)fake_sF;
  return p >= base && p < base + sizeof(fake_sF);
}

size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) return n;
  return fwrite(ptr, size, n, f);
}
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) return 0;
  return fread(ptr, size, n, f);
}
int fputc_fake(int c, FILE *f) { if (is_fake_file(f)) return c; return fputc(c, f); }
int fputs_fake(const char *s, FILE *f) { if (is_fake_file(f)) return 0; return fputs(s, f); }
int fflush_fake(FILE *f) { if (is_fake_file(f) || f == NULL) return 0; return fflush(f); }
int fclose_fake(FILE *f) { if (is_fake_file(f)) return 0; return fclose(f); }
int ferror_fake(FILE *f) { if (is_fake_file(f)) return 0; return ferror(f); }
int feof_fake(FILE *f) { if (is_fake_file(f)) return 1; return feof(f); }
int fileno_fake(FILE *f) { if (is_fake_file(f)) return ((const uint8_t *)f - &fake_sF[0][0]) / 0x100; return fileno(f); }
int fseek_fake(FILE *f, long off, int whence) { if (is_fake_file(f)) return -1; return fseek(f, off, whence); }
long ftell_fake(FILE *f) { if (is_fake_file(f)) return -1; return ftell(f); }
int getc_fake(FILE *f) { if (is_fake_file(f)) return -1; return getc(f); }
int fgetc_fake(FILE *f) { if (is_fake_file(f)) return -1; return fgetc(f); }
char *fgets_fake(char *s, int n, FILE *f) { if (is_fake_file(f)) return NULL; return fgets(s, n, f); }
int ungetc_fake(int c, FILE *f) { if (is_fake_file(f)) return -1; return ungetc(c, f); }
int fprintf_fake(FILE *f, const char *fmt, ...) {
  if (is_fake_file(f)) return 0;
  va_list va; va_start(va, fmt);
  int ret = vfprintf(f, fmt, va);
  va_end(va);
  return ret;
}
int vfprintf_fake(FILE *f, const char *fmt, va_list va) {
  if (is_fake_file(f)) return 0;
  return vfprintf(f, fmt, va);
}

/* Serialize regular-file writes with positioned-write emulation. */
static Mutex g_regular_file_io_lock;
void regular_file_io_lock(void) { mutexLock(&g_regular_file_io_lock); }
void regular_file_io_unlock(void) { mutexUnlock(&g_regular_file_io_lock); }

long pread_fake(int fd, void *buf, size_t count, long offset) {
  if (count == 0) return 0;

  size_t total = 0;
  int failure_errno = 0;
  regular_file_io_lock();
  long current = lseek(fd, 0, SEEK_CUR);
  if (current < 0) {
    failure_errno = errno ? errno : EIO;
  } else if (lseek(fd, (off_t)offset, SEEK_SET) < 0) {
    failure_errno = errno ? errno : EIO;
  }
  while (!failure_errno && total < count) {
    long r = read(fd, (char *)buf + total, count - total);
    if (r < 0) {
      if (errno == EINTR) continue;
      failure_errno = errno ? errno : EIO;
      break;
    }
    if (r == 0) break;
    total += (size_t)r;
  }
  if (current >= 0 && lseek(fd, current, SEEK_SET) < 0 && !failure_errno)
    failure_errno = errno ? errno : EIO;
  regular_file_io_unlock();

  if (failure_errno) {
    errno = failure_errno;
    if (!total) return -1;
  }
  return (long)total;
}

long read_fake(int fd, void *buf, size_t count) {
  if (fakefd_is_fake(fd)) return fakefd_read(fd, buf, count);
  /* Socket reads must return the bytes currently available; waiting to fill a
   * caller's whole buffer can deadlock Unity's network worker. */
  if (nx_net_fd_is_tracked(fd))
    return nx_net_long_result(read(fd, buf, count));
  /* Fill regular-file reads that fsdev returns short. */
  size_t total = 0;
  int failure_errno = 0;
  regular_file_io_lock();
  while (total < count) {
    long r = read(fd, (char *)buf + total, count - total);
    if (r < 0) {
      if (errno == EINTR) continue;
      failure_errno = errno ? errno : EIO;
      break;
    }
    if (r == 0) break;
    total += (size_t)r;
  }
  regular_file_io_unlock();
  if (failure_errno) {
    errno = failure_errno;
    if (!total) return -1;
  }
  return (long)total;
}
long write_fake(int fd, const void *buf, size_t count) {
  if (fakefd_is_fake(fd)) return fakefd_write(fd, buf, count);
  /* Complete regular-file writes; preserve short nonblocking socket writes. */
  if (nx_net_fd_is_tracked(fd))
    return nx_net_long_result(write(fd, buf, count));
  size_t total = 0;
  int failure_errno = 0;
  regular_file_io_lock();
  while (total < count) {
    long r = write(fd, (const char *)buf + total, count - total);
    if (r < 0) {
      if (errno == EINTR) continue;
      failure_errno = errno ? errno : EIO;
      break;
    }
    if (r == 0) {
      failure_errno = EIO;
      break;
    }
    total += (size_t)r;
  }
  regular_file_io_unlock();
  if (failure_errno) {
    errno = failure_errno;
    if (!total) return -1;
  }
  return (long)total;
}
int close_fake(int fd) {
  int network_fd = nx_net_fd_is_tracked(fd);
  fd_ino_clear(fd);
  nx_net_fd_set_tracked(fd, 0);
  if (fakefd_is_fake(fd)) return fakefd_close(fd);
  int result = close(fd);
  return network_fd ? nx_net_int_result(result) : result;
}
int fcntl_fake(int fd, int cmd, ...) {
  if (!nx_net_fd_is_tracked(fd)) return 0; /* preserve Android-host shim */

  if (cmd == 3 /* Android F_GETFL */)
    return g_nonblock_net_fd[fd] ? 0x800 /* Android O_NONBLOCK */ : 0;
  if (cmd == 4 /* Android F_SETFL */) {
    va_list ap;
    va_start(ap, cmd);
    int flags = va_arg(ap, int);
    va_end(ap);
    g_nonblock_net_fd[fd] = (flags & 0x800) != 0;
    if (nx_net_fd_apply_nonblock(fd) < 0) return -1;
    return 0;
  }
  return 0;
}
int ioctl_fake(int fd, unsigned long req, ...) {
  va_list ap;
  va_start(ap, req);
  void *arg = va_arg(ap, void *);
  va_end(ap);
  if (!nx_net_fd_is_tracked(fd)) { errno = ENOTTY; return -1; }

  if (req == 0x5421ul) { /* Android FIONBIO */
    g_nonblock_net_fd[fd] = arg && *(const int *)arg;
    if (nx_net_fd_apply_nonblock(fd) < 0) return -1;
    return 0;
  }
  if (req == 0x541bul) { /* Android FIONREAD */
    if (!arg) { errno = EFAULT; return -1; }
    return nx_net_int_result(ioctl(fd, FIONREAD, arg));
  }
  return 0;
}
int pipe_fake(int fds[2]) { return fakefd_pipe(fds); }
int poll_fake(void *fds, unsigned long nfds, int timeout) {
  struct pollfd *p = (struct pollfd *)fds;
  unsigned long tracked = 0;
  for (unsigned long i = 0; p && i < nfds; i++) {
    if (nx_net_fd_is_tracked(p[i].fd)) tracked++;
    p[i].revents = 0;
  }
  if (!tracked) return 0;

  struct pollfd *native_fds = malloc(nfds * sizeof *native_fds);
  if (!native_fds) { errno = ENOMEM; return -1; }
  for (unsigned long i = 0; i < nfds; i++) {
    native_fds[i] = p[i];
    if (!nx_net_fd_is_tracked(p[i].fd)) native_fds[i].fd = -1;
  }
  int rc = poll(native_fds, (nfds_t)nfds, timeout);
  if (rc >= 0) {
    for (unsigned long i = 0; i < nfds; i++) {
      if (nx_net_fd_is_tracked(p[i].fd))
        p[i].revents = native_fds[i].revents;
    }
  }
  free(native_fds);
  return nx_net_int_result(rc);
}
int select_fake(int n, void *r, void *w, void *e, void *t) {
  fd_set *rf = (fd_set *)r, *wf = (fd_set *)w, *ef = (fd_set *)e;
  fd_set req_r, req_w, req_e, native_r, native_w, native_e;
  if (rf) req_r = *rf;
  if (wf) req_w = *wf;
  if (ef) req_e = *ef;
  FD_ZERO(&native_r); FD_ZERO(&native_w); FD_ZERO(&native_e);

  int maxfd = -1;
  int limit = n < FD_SETSIZE ? n : FD_SETSIZE;
  for (int fd = 0; fd < limit && fd < NX_TRACKED_NET_FDS; fd++) {
    if (!nx_net_fd_is_tracked(fd)) continue;
    int requested = 0;
    if (rf && FD_ISSET(fd, &req_r)) { FD_SET(fd, &native_r); requested = 1; }
    if (wf && FD_ISSET(fd, &req_w)) { FD_SET(fd, &native_w); requested = 1; }
    if (ef && FD_ISSET(fd, &req_e)) { FD_SET(fd, &native_e); requested = 1; }
    if (requested) maxfd = fd;
  }

  if (rf) FD_ZERO(rf);
  if (wf) FD_ZERO(wf);
  if (ef) FD_ZERO(ef);
  if (maxfd < 0) return 0;

  int rc = select(maxfd + 1, rf ? &native_r : NULL, wf ? &native_w : NULL,
                  ef ? &native_e : NULL, (struct timeval *)t);
  if (rc < 0) return nx_net_int_result(rc);
  for (int fd = 0; fd <= maxfd; fd++) {
    if (rf && FD_ISSET(fd, &native_r)) FD_SET(fd, rf);
    if (wf && FD_ISSET(fd, &native_w)) FD_SET(fd, wf);
    if (ef && FD_ISSET(fd, &native_e)) FD_SET(fd, ef);
  }
  return rc;
}

/* Restricted official CDN networking with bionic socket translation. */

struct BionicSockaddrIn {
  uint16_t family;
  uint16_t port;
  uint32_t addr;
  uint8_t zero[8];
};

struct BionicAddrInfo {
  int flags;
  int family;
  int socktype;
  int protocol;
  uint32_t addrlen;
  char *canonname;
  struct BionicSockaddrIn *addr;
  struct BionicAddrInfo *next;
};

static int nx_bootstrap_host_allowed(const char *node) {
  static const char suffix[] = ".akamaized.net";
  if (!node) return 0;
  size_t n = strlen(node), s = sizeof suffix - 1;
  return !strcmp(node, "download-cdn-ac-pocketcamp.akamaized.net") ||
         (n > s && !strcmp(node + n - s, suffix));
}

static int nx_bionic_addr_to_native(const void *addr, unsigned addrlen,
                                    struct sockaddr_in *native_addr) {
  if (!addr || !native_addr || addrlen < sizeof(struct BionicSockaddrIn)) {
    errno = EINVAL;
    return 0;
  }
  const struct BionicSockaddrIn *b =
      (const struct BionicSockaddrIn *)addr;
  if (b->family != 2 /* Android AF_INET */) {
    errno = EAFNOSUPPORT;
    return 0;
  }
  memset(native_addr, 0, sizeof *native_addr);
  native_addr->sin_len = sizeof *native_addr;
  native_addr->sin_family = AF_INET;
  native_addr->sin_port = b->port;
  native_addr->sin_addr.s_addr = b->addr;
  return 1;
}

static int nx_native_addr_to_bionic(const struct sockaddr *addr,
                                    struct BionicSockaddrIn *b,
                                    unsigned *b_len) {
  if (!addr || !b_len || addr->sa_family != AF_INET) {
    errno = EAFNOSUPPORT;
    return 0;
  }
  unsigned capacity = *b_len;
  *b_len = sizeof *b;
  if (!b || capacity < sizeof *b) {
    errno = ENOSPC;
    return 0;
  }
  const struct sockaddr_in *n = (const struct sockaddr_in *)addr;
  memset(b, 0, sizeof *b);
  b->family = 2; /* Android AF_INET */
  b->port = n->sin_port;
  b->addr = n->sin_addr.s_addr;
  return 1;
}

static int nx_msg_flags_from_bionic(int flags) {
  int out = flags & 0x7; /* OOB, PEEK and DONTROUTE are identical. */
  if (flags & 0x40)  out |= MSG_DONTWAIT;
  if (flags & 0x100) out |= MSG_WAITALL;
  /* Android MSG_NOSIGNAL (0x4000) is unnecessary on Horizon. */
  return out;
}

int socket_fake(int d, int t, int p) {
  /* Official endpoints use translated IPv4 sockets. */
  if (d != 2 /* Android AF_INET */) {
    errno = 97; /* Android EAFNOSUPPORT */
    return -1;
  }
  int native_type = t & 0xf;
  int fd = socket(AF_INET, native_type, p);
  nx_net_fd_set_tracked(fd, fd >= 0);
  return nx_net_int_result(fd);
}
int connect_fake(int s, const void *a, unsigned l) {
  struct sockaddr_in n;
  if (!nx_bionic_addr_to_native(a, l, &n))
    return nx_net_int_result(-1);
  return nx_net_int_result(connect(s, (const struct sockaddr *)&n, sizeof n));
}
int bind_fake(int s, const void *a, unsigned l) {
  struct sockaddr_in n;
  if (!nx_bionic_addr_to_native(a, l, &n))
    return nx_net_int_result(-1);
  return nx_net_int_result(bind(s, (const struct sockaddr *)&n, sizeof n));
}
int listen_fake(int s, int b) { return nx_net_int_result(listen(s, b)); }
int accept_fake(int s, void *a, void *l) {
  struct sockaddr_storage n;
  socklen_t nl = sizeof n;
  int fd = accept(s, (struct sockaddr *)&n, a ? &nl : NULL);
  if (fd < 0) return nx_net_int_result(fd);
  nx_net_fd_set_tracked(fd, 1);
  if (!a || !l) return fd;
  unsigned bl = *(unsigned *)l;
  if (!nx_native_addr_to_bionic((const struct sockaddr *)&n,
                                (struct BionicSockaddrIn *)a, &bl)) {
    int saved_errno = errno;
    close(fd);
    nx_net_fd_set_tracked(fd, 0);
    errno = nx_net_errno_to_bionic(saved_errno);
    return -1;
  }
  *(unsigned *)l = bl;
  return fd;
}
long send_fake(int s, const void *b, size_t l, int f) {
  return nx_net_long_result(send(s, b, l, nx_msg_flags_from_bionic(f)));
}
long recv_fake(int s, void *b, size_t l, int f) {
  return nx_net_long_result(recv(s, b, l, nx_msg_flags_from_bionic(f)));
}
long sendto_fake(int s, const void *b, size_t l, int f, const void *a, unsigned al) {
  struct sockaddr_in n;
  if (!nx_bionic_addr_to_native(a, al, &n))
    return nx_net_long_result(-1);
  return nx_net_long_result(sendto(s, b, l, nx_msg_flags_from_bionic(f),
                                   (const struct sockaddr *)&n, sizeof n));
}
long recvfrom_fake(int s, void *b, size_t l, int f, void *a, void *al) {
  struct sockaddr_storage n;
  socklen_t nl = sizeof n;
  long rc = recvfrom(s, b, l, nx_msg_flags_from_bionic(f),
                     a ? (struct sockaddr *)&n : NULL, a ? &nl : NULL);
  if (rc < 0) return nx_net_long_result(rc);
  if (!a || !al) return rc;
  unsigned bl = *(unsigned *)al;
  if (!nx_native_addr_to_bionic((const struct sockaddr *)&n,
                                (struct BionicSockaddrIn *)a, &bl))
    return nx_net_long_result(-1);
  *(unsigned *)al = bl;
  return rc;
}
int shutdown_fake(int s, int how) {
  return nx_net_int_result(shutdown(s, how));
}
int setsockopt_fake(int s, int lv, int n, const void *v, unsigned l) {
  /* Translate socket options needed by CDN and NTP traffic. */
  if (lv == 1 /* Android SOL_SOCKET */) {
    int native_name;
    switch (n) {
      case 2:  native_name = SO_REUSEADDR; break;
      case 7:  native_name = SO_SNDBUF; break;
      case 8:  native_name = SO_RCVBUF; break;
      case 9:  native_name = SO_KEEPALIVE; break;
      case 20: native_name = SO_RCVTIMEO; break;
      case 21: native_name = SO_SNDTIMEO; break;
      default: return 0;
    }
    return nx_net_int_result(
        setsockopt(s, SOL_SOCKET, native_name, v, (socklen_t)l));
  }
  if (lv == 6 /* Android IPPROTO_TCP */ && n == 1 /* TCP_NODELAY */)
    return 0;
  /* Android IP-level multicast/interface hints are not needed by unicast NTP. */
  return 0;
}
int getsockopt_fake(int s, int lv, int n, void *v, void *l) {
  if (!l) { errno = EINVAL; return -1; }
  /* Android SOL_SOCKET/SO_ERROR are 1/4; Curl uses this after connect(). */
  int native_level = lv, native_name = n;
  if (lv == 1) {
    native_level = SOL_SOCKET;
    switch (n) {
      case 2:  native_name = SO_REUSEADDR; break;
      case 3:  native_name = SO_TYPE; break;
      case 4:  native_name = SO_ERROR; break;
      case 7:  native_name = SO_SNDBUF; break;
      case 8:  native_name = SO_RCVBUF; break;
      case 9:  native_name = SO_KEEPALIVE; break;
      case 20: native_name = SO_RCVTIMEO; break;
      case 21: native_name = SO_SNDTIMEO; break;
      default: errno = 92 /* Android ENOPROTOOPT */; return -1;
    }
  }
  socklen_t len = *(unsigned *)l;
  int rc = getsockopt(s, native_level, native_name, v, &len);
  *(unsigned *)l = len;
  if (rc < 0) return nx_net_int_result(rc);
  if (lv == 1 && n == 4 /* Android SO_ERROR */ && v && len >= sizeof(int)) {
    int native_socket_error = *(int *)v;
    *(int *)v = nx_net_errno_to_bionic(native_socket_error);
  }
  return 0;
}
int getsockname_fake(int s, void *a, void *l) {
  if (!l) { errno = EINVAL; return -1; }
  struct sockaddr_storage n;
  socklen_t nl = sizeof n;
  int rc = getsockname(s, (struct sockaddr *)&n, &nl);
  if (rc < 0) return nx_net_int_result(rc);
  if (!a) return rc;
  unsigned bl = *(unsigned *)l;
  if (!nx_native_addr_to_bionic((const struct sockaddr *)&n,
                                (struct BionicSockaddrIn *)a, &bl))
    return nx_net_int_result(-1);
  *(unsigned *)l = bl;
  return 0;
}
int getpeername_fake(int s, void *a, void *l) {
  if (!l) { errno = EINVAL; return -1; }
  struct sockaddr_storage n;
  socklen_t nl = sizeof n;
  int rc = getpeername(s, (struct sockaddr *)&n, &nl);
  if (rc < 0) return nx_net_int_result(rc);
  if (!a) return rc;
  unsigned bl = *(unsigned *)l;
  if (!nx_native_addr_to_bionic((const struct sockaddr *)&n,
                                (struct BionicSockaddrIn *)a, &bl))
    return nx_net_int_result(-1);
  *(unsigned *)l = bl;
  return 0;
}
int getaddrinfo_fake(const char *node, const char *svc, const void *hints, void **res) {
  if (!res) return -1;
  *res = NULL;
  if (!nx_bootstrap_host_allowed(node))
    return -2 /* Android EAI_NONAME */;

  const struct BionicAddrInfo *bh = (const struct BionicAddrInfo *)hints;
  struct addrinfo nh;
  memset(&nh, 0, sizeof nh);
  nh.ai_family = AF_INET; /* Avoid the incompatible Android AF_INET6 value 10. */
  if (bh) {
    nh.ai_flags = bh->flags & AI_CANONNAME;
    nh.ai_socktype = bh->socktype & 0xf;
    nh.ai_protocol = bh->protocol;
  }

  struct addrinfo *native_list = NULL;
  int rc = getaddrinfo(node, svc, &nh, &native_list);
  if (rc != 0) return -2;

  struct BionicAddrInfo *head = NULL, **tail = &head;
  for (const struct addrinfo *n = native_list; n; n = n->ai_next) {
    if (n->ai_family != AF_INET || !n->ai_addr) continue;
    struct BionicAddrInfo *b = calloc(1, sizeof *b);
    struct BionicSockaddrIn *ba = calloc(1, sizeof *ba);
    if (!b || !ba) {
      free(b); free(ba);
      rc = -10; /* Android EAI_MEMORY */
      break;
    }
    const struct sockaddr_in *na = (const struct sockaddr_in *)n->ai_addr;
    ba->family = 2;
    ba->port = na->sin_port;
    ba->addr = na->sin_addr.s_addr;
    b->flags = n->ai_flags;
    b->family = 2;
    b->socktype = n->ai_socktype;
    b->protocol = n->ai_protocol;
    b->addrlen = sizeof *ba;
    b->addr = ba;
    if (n->ai_canonname) b->canonname = strdup(n->ai_canonname);
    *tail = b;
    tail = &b->next;
  }
  freeaddrinfo(native_list);
  if (rc != 0 || !head) {
    freeaddrinfo_fake(head);
    return rc ? rc : -2;
  }
  *res = head;
  return 0;
}
void freeaddrinfo_fake(void *res) {
  struct BionicAddrInfo *b = (struct BionicAddrInfo *)res;
  while (b) {
    struct BionicAddrInfo *next = b->next;
    free(b->canonname);
    free(b->addr);
    free(b);
    b = next;
  }
}
int getnameinfo_fake(const void *a, unsigned al, char *h, unsigned hl,
                     char *s, unsigned sl, int f) {
  struct sockaddr_in n;
  if (!nx_bionic_addr_to_native(a, al, &n)) return -1;
  return getnameinfo((const struct sockaddr *)&n, sizeof n,
                     h, (socklen_t)hl, s, (socklen_t)sl, f);
}
int gethostname_fake(char *name, size_t len) { if (name && len) snprintf(name, len, "switch"); return 0; }
void *getservbyname_fake(const char *n, const char *p) { (void)n; (void)p; return NULL; }
unsigned if_nametoindex_fake(const char *n) { (void)n; return 0; }
char *if_indextoname_fake(unsigned i, char *buf) { (void)i; if (buf) buf[0] = 0; return buf; }

/* Translate Android address-family values to Horizon. */
static int nx_af_from_bionic(int af) {
  if (af == 2 /* Android AF_INET */) return AF_INET;
  if (af == 10 /* Android AF_INET6 */) return AF_INET6;
  errno = EAFNOSUPPORT;
  return -1;
}

const char *inet_ntop_fake(int af, const void *src, char *dst, unsigned size) {
  int native_af = nx_af_from_bionic(af);
  if (native_af < 0) return NULL;
  return inet_ntop(native_af, src, dst, (socklen_t)size);
}

int inet_pton_fake(int af, const char *src, void *dst) {
  int native_af = nx_af_from_bionic(af);
  if (native_af < 0) return -1;
  return inet_pton(native_af, src, dst);
}

int inet_aton_fake(const char *src, void *dst) {
  return inet_pton_fake(2, src, dst) == 1;
}

uint32_t inet_addr_fake(const char *src) {
  uint32_t addr = UINT32_MAX;
  return inet_aton_fake(src, &addr) ? addr : UINT32_MAX;
}

char *inet_ntoa_fake(uint32_t addr) {
  static _Thread_local char text[INET_ADDRSTRLEN];
  return (char *)inet_ntop_fake(2, &addr, text, sizeof text);
}

static volatile int g_h_errno = 0;
int *__get_h_errno_fake(void) { return (int *)&g_h_errno; }

/* Unsupported process-control calls. */

int fork_fake(void) { errno = ENOSYS; return -1; }
int execvp_fake(const char *f, char *const argv[]) { (void)f; (void)argv; errno = ENOSYS; return -1; }
int waitpid_fake(int pid, int *status, int opts) { (void)pid; (void)opts; if (status) *status = 0; errno = ECHILD; return -1; }
int kill_fake(int pid, int sig) { (void)pid; (void)sig; return 0; }
int getpid_fake(void) { return 1; }
int sched_yield_fake(void) { svcSleepThread(0); return 0; }
/* Bionic passwd layout. */
struct bionic_passwd {
  char *pw_name;     /* 0x00 */
  char *pw_passwd;   /* 0x08 */
  uint32_t pw_uid;   /* 0x10 */
  uint32_t pw_gid;   /* 0x14 */
  char *pw_gecos;    /* 0x18 */
  char *pw_dir;      /* 0x20 */
  char *pw_shell;    /* 0x28 */
};
void *getpwuid_fake(int uid) {
  (void)uid;
  static struct bionic_passwd pw;
  static char nm[] = "switch", dir[] = GAME_HOME, sh[] = "/bin/sh", empty[] = "";
  pw.pw_name = nm; pw.pw_passwd = empty; pw.pw_uid = 0; pw.pw_gid = 0;
  pw.pw_gecos = empty; pw.pw_dir = dir; pw.pw_shell = sh;
  return &pw;
}

/* Use GAME_HOME for HOME and TMPDIR. */
const char *managed_path(const char *p) {
  if (!p) return p;
  const char *c = strchr(p, ':');
  return (c && c[1] == '/') ? c + 1 : p;
}
char *getenv_fake(const char *name) {
  if (name) {
    if (!strcmp(name, "HOME"))   return (char *)managed_path(GAME_HOME);
    if (!strcmp(name, "TMPDIR")) return (char *)managed_path(GAME_HOME);
  }
  return getenv(name);
}
/* Return a Unix-rooted managed path. */
char *getcwd_fake(char *buf, size_t size) {
  char *r = getcwd(buf, size);
  if (!r) return r;
  const char *c = strchr(r, ':');
  if (c && c[1] == '/') memmove(r, c + 1, strlen(c + 1) + 1);
  return r;
}
int getrusage_fake(int who, void *usage) { (void)who; if (usage) memset(usage, 0, 144); return 0; }

/* dlopen/dlsym over loaded modules and shims. */

static char g_dlh_default, g_dlh_unity_arcore, g_dlh_arcore_c, g_dlh_arpresto;
static uintptr_t optional_arcore_zero(void) { return 0; }
static intptr_t optional_arcore_fail(void) { return -1; }
static void optional_unity_arcore_session_construct(void *permission_provider) {
  (void)permission_provider;
}
static void optional_arpresto_check(void (*callback)(int, void *), void *context) {
  if (callback) callback(100 /* UnsupportedDeviceNotCapable */, context);
}
static void optional_arpresto_install(int requested,
                                      void (*callback)(int, void *), void *context) {
  (void)requested;
  if (callback) callback(201 /* ErrorDeviceNotCompatible */, context);
}
void *dlopen_fake(const char *name, int flags) {
  (void)flags;
  if (name && strstr(name, "UnityARCore")) return &g_dlh_unity_arcore;
  if (name && strstr(name, "arcore_sdk_c")) return &g_dlh_arcore_c;
  if (name && strstr(name, "arpresto")) return &g_dlh_arpresto;
  return &g_dlh_default;
}
int dlclose_fake(void *h) { (void)h; return 0; }
const char *dlerror_fake(void) { return NULL; }
void *dlsym_fake(void *handle, const char *symbol) {
  if (!symbol) return NULL;
  extern void *firebase_stub_lookup(const char *symbol);

  /* IL2CPP can retry P/Invoke lookups through its default handle instead of
   * the synthetic dlopen handle. Resolve the optional AR plugin by symbol so
   * XR startup reports an unavailable device instead of throwing
   * EntryPointNotFoundException during the boot sequence. */
  if (!strcmp(symbol, "UnityARCore_session_construct"))
    return (void *)&optional_unity_arcore_session_construct;
  if (!strcmp(symbol, "ArPresto_checkApkAvailability"))
    return (void *)&optional_arpresto_check;
  if (!strcmp(symbol, "ArPresto_requestApkInstallation"))
    return (void *)&optional_arpresto_install;
  if (!strncmp(symbol, "UnityARCore_", sizeof("UnityARCore_") - 1) ||
      !strncmp(symbol, "ArPresto_", sizeof("ArPresto_") - 1))
    return (void *)&optional_arcore_zero;

  void *p = so_resolve_external(symbol);
  if (p) return p;
  uintptr_t shim = dynlib_find_export(symbol);
  if (shim) return (void *)shim;
  /* Report optional ARCore services unsupported. */
  if (handle == &g_dlh_unity_arcore || handle == &g_dlh_arpresto) {
    return (void *)&optional_arcore_zero;
  }
  if (handle == &g_dlh_arcore_c) {
    return (void *)&optional_arcore_fail;
  }
  /* Firebase is reported available through local stubs. */
  void *fb = firebase_stub_lookup(symbol);
  if (fb) return fb;
  /* Resolve the full Mesa GLES/EGL surface dynamically. */
  if (!strncmp(symbol, "gl", 2) || !strncmp(symbol, "egl", 3)) {
    p = (void *)eglGetProcAddress(symbol);
    if (p) return p;
  }
  return NULL;
}

/* Pthread rwlocks and semaphores. */

typedef struct { RwLock lock; } FakeRwLock;

static FakeRwLock *get_rwlock(void **storage) {
  if (!storage) return NULL;
  /* Small values are bionic static initializers. */
  if ((uintptr_t)*storage < 0x10000) {
    FakeRwLock *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    rwlockInit(&l->lock);
    *storage = l;
  }
  return *storage;
}
int pthread_rwlock_init_fake(void **rw, const void *attr) {
  (void)attr;
  if (!rw) return EINVAL;
  *rw = NULL;
  return get_rwlock(rw) ? 0 : ENOMEM;
}
int pthread_rwlock_destroy_fake(void **rw) {
  if (rw && (uintptr_t)*rw >= 0x10000) free(*rw);
  if (rw) *rw = NULL;
  return 0;
}
int pthread_rwlock_rdlock_fake(void **rw) {
  FakeRwLock *f = get_rwlock(rw);
  if (!f) return ENOMEM;
  rwlockReadLock(&f->lock);
  return 0;
}
int pthread_rwlock_wrlock_fake(void **rw) {
  FakeRwLock *f = get_rwlock(rw);
  if (!f) return ENOMEM;
  rwlockWriteLock(&f->lock);
  return 0;
}
int pthread_rwlock_unlock_fake(void **rw) {
  FakeRwLock *l = get_rwlock(rw);
  if (!l) return ENOMEM;
  if (rwlockIsWriteLockHeldByCurrentThread(&l->lock)) rwlockWriteUnlock(&l->lock);
  else rwlockReadUnlock(&l->lock);
  return 0;
}

typedef struct { Semaphore sem; } FakeSem;
int sem_init_fake(void **s, int pshared, unsigned int value) { (void)pshared; FakeSem *fs = calloc(1, sizeof(*fs)); semaphoreInit(&fs->sem, value); *s = fs; return 0; }
int sem_destroy_fake(void **s) { if (s && *s) { free(*s); *s = NULL; } return 0; }
int sem_post_fake(void **s) { if (s && *s) semaphoreSignal(&((FakeSem *)*s)->sem); return 0; }
int sem_wait_fake(void **s) {
  if (s && *s) {
    Semaphore *sem = &((FakeSem *)*s)->sem;
    semaphoreWait(sem);
  }
  return 0;
}
int sem_trywait_fake(void **s) { if (s && *s && semaphoreTryWait(&((FakeSem *)*s)->sem)) return 0; errno = EAGAIN; return -1; }
int sem_getvalue_fake(void **s, int *val) { if (s && *s) *val = (int)((FakeSem *)*s)->sem.count; else *val = 0; return 0; }
/* Poll libnx semaphores for timed waits. */
int sem_timedwait_fake(void **s, const struct timespec *abs) {
  if (!abs) { errno = EINVAL; return -1; }
  for (;;) {
    if (sem_trywait_fake(s) == 0) return 0;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    if (now.tv_sec > abs->tv_sec ||
        (now.tv_sec == abs->tv_sec && now.tv_nsec >= abs->tv_nsec)) break;
    svcSleepThread(1000000ull);
  }
  errno = ETIMEDOUT;
  return -1;
}

/* Boehm stop-the-world bridge using libnx thread context APIs. */
uintptr_t g_il2cpp_base = 0;

#define GC_RETRY_SIGNALS_OFF 0x61803b0
#define GC_SUSPEND_SIG_OFF 0x61803b4
#define GC_RESTART_SIG_OFF 0x61803b8
#define GC_STOP_COUNT_OFF  0x63aca70
#define GC_ACK_SEM_OFF     0x63aca80
#define GC_THREADS_OFF     0x63acaa0

typedef struct NxGcThread {
  struct NxGcThread *next;
  pthread_t id;
  volatile uintptr_t last_stop_count;
  volatile uintptr_t stack_ptr;
  uint8_t flags;
  uint8_t thread_blocked;
} NxGcThread;

static unsigned nx_gc_thread_index(pthread_t id) {
  uintptr_t n = (uintptr_t)id;
  uintptr_t x = (n >> 8) ^ n;
  return (unsigned)(((x >> 16) ^ x) & 0xff);
}

static NxGcThread *nx_gc_find_thread(pthread_t id) {
  if (!g_il2cpp_base || !id) return NULL;
  NxGcThread **table = (NxGcThread **)(g_il2cpp_base + GC_THREADS_OFF);
  NxGcThread *p = __atomic_load_n(&table[nx_gc_thread_index(id)], __ATOMIC_ACQUIRE);
  for (int guard = 0; p && guard < 1024; guard++, p = p->next)
    if (p->id == id) return p;
  return NULL;
}

/* Spill register roots into the target stack for Boehm's scan. */
static int nx_gc_pause_and_capture(pthread_t id, NxGcThread *gc_thread) {
  Thread *thread = (Thread *)id;
  Result rc = threadPause(thread);
  if (R_FAILED(rc)) return ESRCH;

  ThreadContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  rc = threadDumpContext(&ctx, thread);
  if (R_FAILED(rc) || !threadContextIsAArch64(&ctx) || ctx.sp < 0x200) {
    threadResume(thread);
    return ESRCH;
  }

  enum { ROOT_WORDS = 34 };
  uintptr_t roots_addr = (ctx.sp - ROOT_WORDS * sizeof(uint64_t)) & ~(uintptr_t)0xf;
  MemoryInfo mi; u32 pi;
  if (R_FAILED(svcQueryMemory(&mi, &pi, roots_addr)) ||
      (mi.perm & Perm_W) == 0 || roots_addr < (uintptr_t)mi.addr ||
      ctx.sp > (uintptr_t)mi.addr + (uintptr_t)mi.size) {
    threadResume(thread);
    return ESRCH;
  }

  uint64_t *roots = (uint64_t *)roots_addr;
  for (int i = 0; i < 29; i++) roots[i] = ctx.cpu_gprs[i].x;
  roots[29] = ctx.fp;
  roots[30] = ctx.lr;
  roots[31] = ctx.sp;
  roots[32] = ctx.pc.x;
  roots[33] = ctx.tpidr;
  __atomic_store_n(&gc_thread->stack_ptr, roots_addr, __ATOMIC_RELEASE);
  return 0;
}

int pthread_kill_gc(pthread_t t, int sig) {
  uintptr_t b = g_il2cpp_base;
  if (b && sig) {
    int suspend_sig = *(volatile int *)(b + GC_SUSPEND_SIG_OFF);
    int restart_sig = *(volatile int *)(b + GC_RESTART_SIG_OFF);
    void **ack_sem  = (void **)(b + GC_ACK_SEM_OFF);
    NxGcThread *gc_thread = nx_gc_find_thread(t);
    if (sig == suspend_sig) {
      if (!gc_thread) return ESRCH;
      int err = nx_gc_pause_and_capture(t, gc_thread);
      if (err) return err;
      uintptr_t stop_count = __atomic_load_n(
          (volatile uintptr_t *)(b + GC_STOP_COUNT_OFF), __ATOMIC_ACQUIRE);
      __atomic_store_n(&gc_thread->last_stop_count, stop_count, __ATOMIC_RELEASE);
      sem_post_fake(ack_sem);
      return 0;
    }
    if (sig == restart_sig) {
      if (!gc_thread) return ESRCH;
      Result rc = threadResume((Thread *)t);
      if (R_FAILED(rc)) return ESRCH;
      if (*(volatile int *)(b + GC_RETRY_SIGNALS_OFF)) {
        uintptr_t stop_count = __atomic_load_n(
            (volatile uintptr_t *)(b + GC_STOP_COUNT_OFF), __ATOMIC_ACQUIRE);
        __atomic_store_n(&gc_thread->last_stop_count, stop_count | 1, __ATOMIC_RELEASE);
        sem_post_fake(ack_sem);
      }
      return 0;
    }
  }
  return 0;
}
