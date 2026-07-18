/* Android/Unity import resolution.
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
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
#include <strings.h>
#include <malloc.h>
extern int z_strncasecmp(const char *, const char *, unsigned long);
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <wchar.h>
#include <errno.h>
#include <locale.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <switch.h>
#include "config.h"
#include "so_util.h"
#include "util.h"
#include "libc_shim.h"
#include "opensles.h"
#include "imports.h"
#include "unity_imports.h"

extern int *__errno(void);

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
  (void)tag;
  (void)fmt;
  return 0;
}
int __android_log_write(int prio, const char *tag, const char *text) {
  (void)prio;
  (void)tag;
  return text ? (int)strlen(text) : 0;
}
int __android_log_buf_write(int buffer, int prio, const char *tag, const char *text) {
  (void)buffer;
  return __android_log_write(prio, tag, text);
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list va) {
  (void)prio;
  (void)tag;
  (void)fmt;
  (void)va;
  return 0;
}

void abort_fake(void) {
  abort();
  __builtin_unreachable();
}
void exit_fake(int code) {
  exit(code);
  __builtin_unreachable();
}
int raise_fake(int sig) { return raise(sig); }
void __assert2(const char *file, int line, const char *func, const char *expr) {
  (void)file; (void)line; (void)func; (void)expr;
  abort_fake();
}

uint64_t __stack_chk_guard_fake = 0x0ull;
void __stack_chk_fail_fake(void) { abort_fake(); }

int  __cxa_atexit_fake(void (*fn)(void *), void *arg, void *dso) { (void)fn; (void)arg; (void)dso; return 0; }
void __cxa_finalize_fake(void *dso) { (void)dso; }

FILE *stderr_fake = (FILE *)&fake_sF[2];

/* Heap-backed bionic pthread objects. */

#define BIONIC_OBJECT_BUSY 0xfffffff0u

static int bionic_object_ready(uint32_t low) {
  return (low & 1u) != 0;
}

static void *bionic_object_decode(const uint32_t *words, uint32_t low) {
  uintptr_t encoded = (uintptr_t)low |
      ((uintptr_t)__atomic_load_n(&words[1], __ATOMIC_RELAXED) << 32);
  return (void *)(encoded & ~(uintptr_t)1);
}

static void bionic_object_publish(void *storage, void *object) {
  uint32_t *words = storage;
  uintptr_t encoded = (uintptr_t)object | 1u;
  __atomic_store_n(&words[1], (uint32_t)(encoded >> 32), __ATOMIC_RELAXED);
  __atomic_store_n(&words[0], (uint32_t)encoded, __ATOMIC_RELEASE);
}

static void *bionic_object_take(void *storage) {
  uint32_t *words = storage;
  for (;;) {
    uint32_t low = __atomic_load_n(&words[0], __ATOMIC_ACQUIRE);
    if (low == BIONIC_OBJECT_BUSY) {
      svcSleepThread(1000);
      continue;
    }
    if (!bionic_object_ready(low)) return NULL;
    uint32_t expected = low;
    if (__atomic_compare_exchange_n(&words[0], &expected, BIONIC_OBJECT_BUSY,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      return bionic_object_decode(words, low);
  }
}

static void bionic_object_clear(void *storage) {
  uint32_t *words = storage;
  __atomic_store_n(&words[1], 0, __ATOMIC_RELAXED);
  __atomic_store_n(&words[0], 0, __ATOMIC_RELEASE);
}

static int create_mutex(int recursive, pthread_mutex_t **out) {
  pthread_mutex_t *m = calloc(1, sizeof(*m));
  if (!m) return ENOMEM;
  int ret;
  if (recursive) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    ret = pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
  } else {
    ret = pthread_mutex_init(m, NULL);
  }
  if (ret != 0) {
    free(m);
    return ret;
  }
  *out = m;
  return 0;
}

int pthread_mutex_init_fake(pthread_mutex_t **uid, const int *attr) {
  if (!uid) return EINVAL;
  pthread_mutex_t *m;
  int ret = create_mutex(attr && *attr == 1, &m);
  if (ret != 0) return ret;
  bionic_object_publish(uid, m);
  return 0;
}
int pthread_mutex_destroy_fake(pthread_mutex_t **uid) {
  if (!uid) return EINVAL;
  pthread_mutex_t *m = bionic_object_take(uid);
  if (m) {
    pthread_mutex_destroy(m);
    free(m);
    bionic_object_clear(uid);
  }
  return 0;
}
static int ensure_mutex(pthread_mutex_t **uid, pthread_mutex_t **out) {
  if (!uid) return EINVAL;
  uint32_t *words = (uint32_t *)uid;
  for (;;) {
    uint32_t value = __atomic_load_n(&words[0], __ATOMIC_ACQUIRE);
    if (value == BIONIC_OBJECT_BUSY) {
      svcSleepThread(1000);
      continue;
    }
    if (bionic_object_ready(value)) {
      *out = bionic_object_decode(words, value);
      return 0;
    }

    uint32_t expected = value;
    if (!__atomic_compare_exchange_n(&words[0], &expected, BIONIC_OBJECT_BUSY,
                                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      continue;

    pthread_mutex_t *m;
    int ret = create_mutex(value == 0x4000, &m);
    if (ret != 0) {
      __atomic_store_n(&words[0], value, __ATOMIC_RELEASE);
      return ret;
    }
    bionic_object_publish(uid, m);
    *out = m;
    return 0;
  }
}
int pthread_mutex_lock_fake(pthread_mutex_t **uid) {
  pthread_mutex_t *m;
  int ret = ensure_mutex(uid, &m);
  if (ret != 0) return ret;
  return pthread_mutex_lock(m);
}
int pthread_mutex_trylock_fake(pthread_mutex_t **uid) { pthread_mutex_t *m; int ret = ensure_mutex(uid, &m); if (ret != 0) return ret; return pthread_mutex_trylock(m); }
int pthread_mutex_unlock_fake(pthread_mutex_t **uid) { pthread_mutex_t *m; int ret = ensure_mutex(uid, &m); if (ret != 0) return ret; return pthread_mutex_unlock(m); }
int pthread_mutex_timedlock_fake(pthread_mutex_t **uid, const struct timespec *abs) {
  (void)abs;
  pthread_mutex_t *m;
  int ret = ensure_mutex(uid, &m);
  if (ret != 0) return ret;
  for (int i = 0; i < 1000; i++) {
    if (pthread_mutex_trylock(m) == 0) return 0;
    svcSleepThread(1000000ull);
  }
  return ETIMEDOUT;
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *attr) {
  (void)attr;
  if (!cnd) return EINVAL;
  pthread_cond_t *c = calloc(1, sizeof(*c));
  if (!c) return ENOMEM;
  int ret = pthread_cond_init(c, NULL);
  if (ret != 0) { free(c); return ret; }
  bionic_object_publish(cnd, c);
  return 0;
}
static int ensure_cond(pthread_cond_t **cnd, pthread_cond_t **out) {
  if (!cnd) return EINVAL;
  uint32_t *words = (uint32_t *)cnd;
  for (;;) {
    uint32_t value = __atomic_load_n(&words[0], __ATOMIC_ACQUIRE);
    if (value == BIONIC_OBJECT_BUSY) {
      svcSleepThread(1000);
      continue;
    }
    if (bionic_object_ready(value)) {
      *out = bionic_object_decode(words, value);
      return 0;
    }

    uint32_t expected = value;
    if (!__atomic_compare_exchange_n(&words[0], &expected, BIONIC_OBJECT_BUSY,
                                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      continue;

    pthread_cond_t *c = calloc(1, sizeof(*c));
    if (!c) {
      __atomic_store_n(&words[0], value, __ATOMIC_RELEASE);
      return ENOMEM;
    }
    int ret = pthread_cond_init(c, NULL);
    if (ret != 0) {
      free(c);
      __atomic_store_n(&words[0], value, __ATOMIC_RELEASE);
      return ret;
    }
    bionic_object_publish(cnd, c);
    *out = c;
    return 0;
  }
}
int pthread_cond_broadcast_fake(pthread_cond_t **cnd) { pthread_cond_t *c; int ret = ensure_cond(cnd, &c); if (ret != 0) return ret; return pthread_cond_broadcast(c); }
int pthread_cond_signal_fake(pthread_cond_t **cnd) { pthread_cond_t *c; int ret = ensure_cond(cnd, &c); if (ret != 0) return ret; return pthread_cond_signal(c); }
int pthread_cond_destroy_fake(pthread_cond_t **cnd) {
  if (!cnd) return EINVAL;
  pthread_cond_t *c = bionic_object_take(cnd);
  if (c) {
    pthread_cond_destroy(c);
    free(c);
    bionic_object_clear(cnd);
  }
  return 0;
}
int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx) {
  pthread_cond_t *c;
  pthread_mutex_t *m;
  int ret = ensure_cond(cnd, &c);
  if (ret != 0) return ret;
  ret = ensure_mutex(mtx, &m);
  if (ret != 0) return ret;
  return pthread_cond_wait(c, m);
}
int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t) {
  pthread_cond_t *c;
  pthread_mutex_t *m;
  int ret = ensure_cond(cnd, &c);
  if (ret != 0) return ret;
  ret = ensure_mutex(mtx, &m);
  if (ret != 0) return ret;
  return t ? pthread_cond_timedwait(c, m, t)
           : pthread_cond_wait(c, m);
}

int pthread_once_fake(volatile int *once, void (*init)(void)) {
  if (!once || !init) return -1;
  /* 0=uninitialized, 1=running, 2=complete. */
  if (__sync_bool_compare_and_swap(once, 0, 1)) {
    (*init)();
    __atomic_store_n(once, 2, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(once, __ATOMIC_ACQUIRE) != 2)
      svcSleepThread(100000);
  }
  return 0;
}

int pthread_mutexattr_init_fake(int *a) { if (a) *a = 0; return 0; }
int pthread_mutexattr_settype_fake(int *a, int t) { if (a) *a = t; return 0; }

/* Minimal bionic pthread attributes. */
#define ATTR_MAGIC 0x41545452 /* 'ATTR' */
typedef struct { uint32_t magic; uint32_t detach; size_t stacksize; } OurAttr;

int pthread_attr_init_fake(void *a) { if (a) { OurAttr *o = a; o->magic = ATTR_MAGIC; o->detach = 0; o->stacksize = 0; } return 0; }
int pthread_attr_destroy_fake(void *a) { (void)a; return 0; }
int pthread_attr_setdetachstate_fake(void *a, int s) {
  if (!a || (s != PTHREAD_CREATE_JOINABLE && s != PTHREAD_CREATE_DETACHED))
    return EINVAL;
  OurAttr *o = a;
  if (o->magic != ATTR_MAGIC) return EINVAL;
  o->detach = (uint32_t)s;
  return 0;
}
int pthread_attr_setstacksize_fake(void *a, size_t s) { if (a) { OurAttr *o = a; if (o->magic == ATTR_MAGIC) o->stacksize = s; } return 0; }

typedef struct {
  void *(*entry)(void *);
  void *arg;
  uint8_t tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  void *native_tls;
} ThreadStart;

static void drain_fake_thread_keys(void);

static void thread_start_cleanup(void *p) {
  ThreadStart *ts = p;
  drain_fake_thread_keys();
  armSetTlsRw(ts->native_tls);
  free(ts);
}

static void *thread_trampoline(void *p) {
  ThreadStart *ts = (ThreadStart *)p;
  void *ret;
  ts->native_tls = armGetTlsRw();
  pthread_cleanup_push(thread_start_cleanup, ts);
  install_bionic_tls(ts->tls);
  ret = ts->entry(ts->arg);
  pthread_cleanup_pop(1);
  return ret;
}
int pthread_create_fake(pthread_t *thread, const void *bionic_attr, void *entry, void *arg) {
  if (!thread || !entry) return EINVAL;
  ThreadStart *ts = malloc(sizeof(*ts));
  if (!ts) return EAGAIN;
  ts->entry = (void *(*)(void *))entry;
  ts->arg = arg;
  size_t stack = 0;
  int detach = PTHREAD_CREATE_JOINABLE;
  if (bionic_attr) {
    const OurAttr *o = bionic_attr;
    if (o->magic == ATTR_MAGIC) {
      stack = o->stacksize;
      detach = (int)o->detach;
    }
  }
  if (stack < (2u << 20)) stack = 2u << 20;
  pthread_attr_t attr;
  int r = pthread_attr_init(&attr);
  if (r != 0) { free(ts); return r; }
  r = pthread_attr_setstacksize(&attr, stack);
  if (r == 0) r = pthread_attr_setdetachstate(&attr, detach);
  if (r == 0) r = pthread_create(thread, &attr, thread_trampoline, ts);
  pthread_attr_destroy(&attr);
  if (r != 0) { free(ts); return r; }
  return 0;
}
int pthread_join_fake(pthread_t thread, void **retval) {
  return pthread_join(thread, retval);
}
int pthread_setschedparam_fake(pthread_t t, int policy, const void *p) { (void)t; (void)policy; (void)p; return 0; }
int pthread_sigmask_fake(int how, const void *set, void *old) { (void)how; (void)set; (void)old; return 0; }

/* Multiplex bionic TLS keys over one newlib key. */
#define FAKE_KEYS_MAX 128
static pthread_mutex_t g_key_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct { int used; void (*dtor)(void *); } g_key_table[FAKE_KEYS_MAX];
static pthread_key_t g_master_key;
static int g_master_key_ready;
typedef struct { void *values[FAKE_KEYS_MAX]; } KeyValues;

static void master_key_dtor(void *p) {
  KeyValues *kv = p;
  for (int iter = 0; iter < 4; iter++) {
    int again = 0;
    for (int i = 0; i < FAKE_KEYS_MAX; i++) {
      void *v = kv->values[i];
      if (g_key_table[i].used && g_key_table[i].dtor && v) {
        kv->values[i] = NULL;
        g_key_table[i].dtor(v);
        again = 1;
      }
    }
    if (!again) break;
  }
  free(kv);
}

static void drain_fake_thread_keys(void) {
  if (!g_master_key_ready) return;
  for (int iter = 0; iter < 4; iter++) {
    KeyValues *kv = pthread_getspecific(g_master_key);
    if (!kv) break;
    pthread_setspecific(g_master_key, NULL);
    master_key_dtor(kv);
  }
}

int pthread_key_create_fake(unsigned *key, void (*dtor)(void *)) {
  pthread_mutex_lock(&g_key_mutex);
  if (!g_master_key_ready) {
    if (pthread_key_create(&g_master_key, master_key_dtor) != 0) {
      pthread_mutex_unlock(&g_key_mutex);
      return EAGAIN;
    }
    g_master_key_ready = 1;
  }
  for (unsigned i = 0; i < FAKE_KEYS_MAX; i++) {
    if (!g_key_table[i].used) {
      g_key_table[i].used = 1;
      g_key_table[i].dtor = dtor;
      *key = i + 1;
      pthread_mutex_unlock(&g_key_mutex);
      return 0;
    }
  }
  pthread_mutex_unlock(&g_key_mutex);
  return EAGAIN;
}

int pthread_key_delete_fake(unsigned key) {
  if (key == 0 || key > FAKE_KEYS_MAX) return EINVAL;
  pthread_mutex_lock(&g_key_mutex);
  g_key_table[key - 1].used = 0;
  g_key_table[key - 1].dtor = NULL;
  pthread_mutex_unlock(&g_key_mutex);
  return 0;
}

void *pthread_getspecific_fake(unsigned key) {
  if (key == 0 || key > FAKE_KEYS_MAX || !g_master_key_ready) return NULL;
  KeyValues *kv = pthread_getspecific(g_master_key);
  return kv ? kv->values[key - 1] : NULL;
}

int pthread_setspecific_fake(unsigned key, const void *value) {
  if (key == 0 || key > FAKE_KEYS_MAX || !g_master_key_ready) return EINVAL;
  KeyValues *kv = pthread_getspecific(g_master_key);
  if (!kv) {
    kv = calloc(1, sizeof(*kv));
    if (!kv) return ENOMEM;
    pthread_setspecific(g_master_key, kv);
  }
  kv->values[key - 1] = (void *)value;
  return 0;
}

static int ret0_i(void) { return 0; }
static unsigned ret0_u(void) { return 0; }
static int signal_stub(int s, void *h) { (void)s; (void)h; return 0; }
static int sigaction_stub(int s, const void *a, void *o) { (void)s; (void)a; (void)o; return 0; }
static int tcgetattr_stub(int fd, void *t) { (void)fd; if (t) memset(t, 0, 60); return 0; }
static int tcsetattr_stub(int fd, int opt, const void *t) { (void)fd; (void)opt; (void)t; return 0; }

/* Asset download file operations. */
static int chmod_stub(const char *path, int mode) { (void)path; (void)mode; return 0; }
static int truncate_impl(const char *path, long len) {
  regular_file_io_lock();
  int r = truncate(path, (off_t)len);
  int saved_errno = errno;
  regular_file_io_unlock();
  errno = saved_errno;
  return r;
}
static int ftruncate_impl(int fd, long len) {
  regular_file_io_lock();
  int r = ftruncate(fd, (off_t)len);
  int saved_errno = errno;
  regular_file_io_unlock();
  errno = saved_errno;
  return r;
}
static int fsync_impl(int fd) {
  regular_file_io_lock();
  int r = fsync(fd);
  int saved_errno = errno;
  regular_file_io_unlock();
  errno = saved_errno;
  return r;
}
static int dup2_stub(int a, int b) { (void)a; return b; }
static long pread_impl(int fd, void *buf, size_t n, long off) {
  long cur = lseek(fd, 0, SEEK_CUR);
  if (cur < 0) return -1;
  if (lseek(fd, off, SEEK_SET) < 0) return -1;
  size_t total = 0;
  while (total < n) {
    long r = read(fd, (char *)buf + total, n - total);
    if (r <= 0) break;
    total += (size_t)r;
  }
  lseek(fd, cur, SEEK_SET);
  return (long)total;
}
static long pwrite_impl(int fd, const void *buf, size_t n, long off) {
  size_t total = 0;
  int failure_errno = 0;
  regular_file_io_lock();
  long cur = lseek(fd, 0, SEEK_CUR);
  if (cur < 0) {
    failure_errno = errno ? errno : EIO;
  } else if (lseek(fd, (off_t)off, SEEK_SET) < 0) {
    failure_errno = errno ? errno : EIO;
  }
  while (total < n) {
    if (failure_errno) break;
    long r = write(fd, (const char *)buf + total, n - total);
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
  if (cur >= 0 && lseek(fd, cur, SEEK_SET) < 0 && !failure_errno)
    failure_errno = errno ? errno : EIO;
  regular_file_io_unlock();
  if (failure_errno) {
    errno = failure_errno;
    if (!total) return -1;
  }
  return (long)total;
}
static int uname_fake(void *buf) { if (buf) memset(buf, 0, 390); return 0; }
static long sysconf_pass(int n) { return sysconf_fake(n); }
static char *g_tzname_fake[2] = { (char *)"UTC", (char *)"UTC" };
static int readlink_stub(const char *p, char *b, size_t n) { (void)p; (void)b; (void)n; errno = EINVAL; return -1; }
static int link_stub(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
static int symlink_stub(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
static int fchmod_stub(int fd, int m) { (void)fd; (void)m; return 0; }
static int fchmodat_stub(int d, const char *p, int m, int f) { (void)d; (void)p; (void)m; (void)f; return 0; }
static int utimensat_stub(int d, const char *p, const void *t, int f) { (void)d; (void)p; (void)t; (void)f; return 0; }
static long sendfile_impl(int out_fd, int in_fd, long *offset, size_t count) {
  const size_t chunk_size = 64 * 1024;
  unsigned char *buf = malloc(chunk_size);
  if (!buf) { errno = ENOMEM; return -1; }

  long saved_pos = -1;
  if (offset) {
    saved_pos = lseek(in_fd, 0, SEEK_CUR);
    if (saved_pos < 0 || lseek(in_fd, *offset, SEEK_SET) < 0) {
      const int saved_errno = errno;
      free(buf);
      errno = saved_errno;
      return -1;
    }
  }

  size_t copied = 0;
  int failure_errno = 0;
  while (copied < count) {
    size_t want = count - copied;
    if (want > chunk_size) want = chunk_size;

    ssize_t nr;
    do { nr = read(in_fd, buf, want); } while (nr < 0 && errno == EINTR);
    if (nr == 0) break;
    if (nr < 0) { failure_errno = errno ? errno : EIO; break; }

    size_t written = 0;
    while (written < (size_t)nr) {
      ssize_t nw;
      do { nw = write(out_fd, buf + written, (size_t)nr - written); }
      while (nw < 0 && errno == EINTR);
      if (nw <= 0) {
        failure_errno = (nw < 0 && errno) ? errno : EIO;
        break;
      }
      written += (size_t)nw;
      copied += (size_t)nw;
    }
    if (written != (size_t)nr) break;
  }

  if (offset) {
    *offset += (long)copied;
    const int restore_errno = failure_errno;
    if (lseek(in_fd, saved_pos, SEEK_SET) < 0 && !failure_errno)
      failure_errno = errno ? errno : EIO;
    if (restore_errno) errno = restore_errno;
  }

  free(buf);
  if (failure_errno && copied == 0) {
    errno = failure_errno;
    return -1;
  }
  if (failure_errno) errno = failure_errno;
  return (long)copied;
}
static void *fdopendir_stub(int fd) { (void)fd; errno = ENOSYS; return NULL; }
static void _exit_fake(int code) {
  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(code);
}

/* JNI may supply NULL device strings; treat them as empty. */
static int z_strcmp(const char *a, const char *b) {
  if (a == b) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return strcmp(a, b);
}
static int z_strncmp(const char *a, const char *b, size_t n) {
  if (a == b || n == 0) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return strncmp(a, b, n);
}
static char *z_strstr(const char *h, const char *n) {
  if (!h || !n) return NULL;
  return strstr(h, n);
}
static char *z_strchr(const char *s, int c) { return s ? strchr(s, c) : NULL; }
static char *z_strrchr(const char *s, int c) { return s ? strrchr(s, c) : NULL; }
static size_t z_strlen(const char *s) { return s ? strlen(s) : 0; }

/* NDK functions implemented by platform shims. */
extern void *ALooper_prepare(int);
extern int   ALooper_addFd(void *, int, int, int, void *, void *);
extern int   ALooper_pollOnce(int, int *, int *, void **);
extern void  AInputQueue_attachLooper(void *, void *, int, void *, void *);
extern void  AInputQueue_detachLooper(void *);
extern int32_t AInputQueue_getEvent(void *, void **);
extern int32_t AInputQueue_preDispatchEvent(void *, void *);
extern void  AInputQueue_finishEvent(void *, void *, int);
extern int32_t AInputEvent_getType(const void *);
extern int32_t AMotionEvent_getAction(const void *);
extern size_t  AMotionEvent_getPointerCount(const void *);
extern int32_t AMotionEvent_getPointerId(const void *, size_t);
extern float   AMotionEvent_getX(const void *, size_t);
extern float   AMotionEvent_getY(const void *, size_t);
extern int32_t AKeyEvent_getKeyCode(const void *);
extern int32_t AKeyEvent_getFlags(const void *);
extern int32_t AKeyEvent_getRepeatCount(const void *);
extern int32_t ANativeWindow_setBuffersGeometry(void *, int32_t, int32_t, int32_t);
extern void *AConfiguration_new(void);
extern void  AConfiguration_fromAssetManager(void *, void *);
extern void  AConfiguration_getLanguage(void *, char *);
extern void  AConfiguration_getCountry(void *, char *);
extern void  AConfiguration_delete(void *);
extern int   ASensorEventQueue_enableSensor(void *, const void *);
extern int   ASensorEventQueue_disableSensor(void *, const void *);
extern int   ASensorEventQueue_setEventRate(void *, const void *, int32_t);
extern int   ASensorEventQueue_getEvents(void *, void *, size_t);
extern void *AAssetManager_fromJava(void *, void *);
extern void *AAssetManager_open(void *, const char *, int);
extern const void *AAsset_getBuffer(void *);
extern int64_t AAsset_getLength(void *);
extern void  AAsset_close(void *);
/* Keep Unity's configured surface size during Mesa initialization. */
static EGLBoolean egl_QuerySurface(EGLDisplay d, EGLSurface s, EGLint attr, EGLint *val) {
  EGLBoolean r = eglQuerySurface(d, s, attr, val);
  if (val) {
    if (attr == 0x3057 /*EGL_WIDTH*/  && *val <= 0) { *val = screen_width;  r = EGL_TRUE; }
    if (attr == 0x3056 /*EGL_HEIGHT*/ && *val <= 0) { *val = screen_height; r = EGL_TRUE; }
  }
  return r;
}

extern void android_native_draw_cursor(void);   /* docked cursor overlay (android_native_unity.c) */

static EGLBoolean egl_SwapBuffers_present(EGLDisplay d, EGLSurface s) {
  android_native_draw_cursor();          /* overlay the docked cursor, then present */
  return eglSwapBuffers(d, s);
}

DynLibFunction dynlib_functions[] = {
  /* liblog, cxxabi, and fortify. */
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_write", (uintptr_t)&__android_log_write },
  { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
  { "android_set_abort_message", (uintptr_t)&android_set_abort_message_fake },
  { "__assert2", (uintptr_t)&__assert2 },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit_fake },
  { "__cxa_finalize", (uintptr_t)&__cxa_finalize_fake },
  { "__cxa_thread_atexit_impl", (uintptr_t)&__cxa_thread_atexit_impl_fake },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail_fake },
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
  { "__errno", (uintptr_t)&__errno },
  { "__get_h_errno", (uintptr_t)&__get_h_errno_fake },

  { "__memcpy_chk", (uintptr_t)&__memcpy_chk_fake },
  { "__memmove_chk", (uintptr_t)&__memmove_chk_fake },
  { "__memset_chk", (uintptr_t)&__memset_chk_fake },
  { "__strcat_chk", (uintptr_t)&__strcat_chk_fake },
  { "__strchr_chk", (uintptr_t)&__strchr_chk_fake },
  { "__strcpy_chk", (uintptr_t)&__strcpy_chk_fake },
  { "__strlen_chk", (uintptr_t)&__strlen_chk_fake },
  { "__strncat_chk", (uintptr_t)&__strncat_chk_fake },
  { "__strncpy_chk", (uintptr_t)&__strncpy_chk_fake },
  { "__strncpy_chk2", (uintptr_t)&__strncpy_chk2_fake },
  { "__strrchr_chk", (uintptr_t)&__strrchr_chk_fake },
  { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_fake },
  { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk_fake },
  { "__snprintf_chk", (uintptr_t)&__snprintf_chk_fake },
  { "__sprintf_chk", (uintptr_t)&__sprintf_chk_fake },
  { "__open_2", (uintptr_t)&__open_2_fake },
  { "__read_chk", (uintptr_t)&__read_chk_fake },
  { "__pread_chk", (uintptr_t)&__pread_chk_fake },
  { "__FD_SET_chk", (uintptr_t)&__FD_SET_chk_fake },
  { "__FD_ISSET_chk", (uintptr_t)&__FD_ISSET_chk_fake },

  /* Bionic. */
  { "__system_property_get", (uintptr_t)&__system_property_get_fake },
  { "getauxval", (uintptr_t)&getauxval_fake },
  { "syscall", (uintptr_t)&syscall_fake },
  { "dl_iterate_phdr", (uintptr_t)&so_dl_iterate_phdr },
  { "__register_atfork", (uintptr_t)&__register_atfork_fake },
  { "__ctype_get_mb_cur_max", (uintptr_t)&__ctype_get_mb_cur_max_fake },
  { "sysconf", (uintptr_t)&sysconf_pass },
  { "pathconf", (uintptr_t)&pathconf_fake },
  { "uname", (uintptr_t)&uname_fake },
  { "openlog", (uintptr_t)&ret0_i },
  { "closelog", (uintptr_t)&ret0_i },
  { "syslog", (uintptr_t)&ret0_i },
  { "abort", (uintptr_t)&abort_fake },
  { "_exit", (uintptr_t)&_exit_fake },

  { "malloc", (uintptr_t)&malloc },
  { "calloc", (uintptr_t)&calloc },
  { "realloc", (uintptr_t)&realloc },
  { "free", (uintptr_t)&free },
  { "memalign", (uintptr_t)&memalign },
  { "posix_memalign", (uintptr_t)&posix_memalign_fake },
  { "mmap", (uintptr_t)&mmap_fake },
  { "munmap", (uintptr_t)&munmap_fake },
  { "mprotect", (uintptr_t)&mprotect_fake },
  { "madvise", (uintptr_t)&madvise_fake },

  { "memchr", (uintptr_t)&memchr }, { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy }, { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "strcat", (uintptr_t)&strcat }, { "strchr", (uintptr_t)&z_strchr },
  { "strcmp", (uintptr_t)&z_strcmp }, { "strcpy", (uintptr_t)&strcpy },
  { "strlen", (uintptr_t)&z_strlen }, { "strncasecmp", (uintptr_t)&z_strncasecmp },
  { "strncmp", (uintptr_t)&z_strncmp }, { "strncpy", (uintptr_t)&strncpy },
  { "strrchr", (uintptr_t)&z_strrchr }, { "strstr", (uintptr_t)&z_strstr },
  { "strtod", (uintptr_t)&strtod }, { "strtof", (uintptr_t)&strtof },
  { "strtol", (uintptr_t)&strtol }, { "strtold", (uintptr_t)&strtold },
  { "strtoll", (uintptr_t)&strtoll }, { "strtoul", (uintptr_t)&strtoul },
  { "strtoull", (uintptr_t)&strtoull }, { "atoi", (uintptr_t)&atoi },
  { "qsort", (uintptr_t)&qsort }, { "rand", (uintptr_t)&rand }, { "srand", (uintptr_t)&srand },
  { "isalnum", (uintptr_t)&isalnum }, { "isspace", (uintptr_t)&isspace },
  { "isupper", (uintptr_t)&isupper }, { "isxdigit", (uintptr_t)&isxdigit },
  { "tolower", (uintptr_t)&tolower },

  { "wcslen", (uintptr_t)&wcslen }, { "wmemchr", (uintptr_t)&wmemchr },
  { "wmemcmp", (uintptr_t)&wmemcmp }, { "wcstod", (uintptr_t)&wcstod },
  { "wcstof", (uintptr_t)&wcstof }, { "wcstol", (uintptr_t)&wcstol },
  { "wcstold", (uintptr_t)&wcstold }, { "wcstoll", (uintptr_t)&wcstoll },
  { "wcstoul", (uintptr_t)&wcstoul }, { "wcstoull", (uintptr_t)&wcstoull },
  { "btowc", (uintptr_t)&btowc }, { "wctob", (uintptr_t)&wctob },
  { "mbrlen", (uintptr_t)&mbrlen }, { "mbrtowc", (uintptr_t)&mbrtowc },
  { "mbtowc", (uintptr_t)&mbtowc }, { "mbsrtowcs", (uintptr_t)&mbsrtowcs },
  { "wcrtomb", (uintptr_t)&wcrtomb }, { "mbsnrtowcs", (uintptr_t)&mbsnrtowcs_fake },
  { "wcsnrtombs", (uintptr_t)&wcsnrtombs_fake },
  { "setlocale", (uintptr_t)&setlocale }, { "localeconv", (uintptr_t)&localeconv },
  { "newlocale", (uintptr_t)&newlocale_fake }, { "freelocale", (uintptr_t)&freelocale_fake },
  { "uselocale", (uintptr_t)&uselocale_fake },
  { "iswalpha_l", (uintptr_t)&iswalpha_l_fake }, { "iswblank_l", (uintptr_t)&iswblank_l_fake },
  { "iswcntrl_l", (uintptr_t)&iswcntrl_l_fake }, { "iswdigit_l", (uintptr_t)&iswdigit_l_fake },
  { "iswlower_l", (uintptr_t)&iswlower_l_fake }, { "iswprint_l", (uintptr_t)&iswprint_l_fake },
  { "iswpunct_l", (uintptr_t)&iswpunct_l_fake }, { "iswspace_l", (uintptr_t)&iswspace_l_fake },
  { "iswupper_l", (uintptr_t)&iswupper_l_fake }, { "iswxdigit_l", (uintptr_t)&iswxdigit_l_fake },
  { "towlower_l", (uintptr_t)&towlower_l_fake }, { "towupper_l", (uintptr_t)&towupper_l_fake },
  { "strcoll_l", (uintptr_t)&strcoll_l_fake }, { "strxfrm_l", (uintptr_t)&strxfrm_l_fake },
  { "strftime_l", (uintptr_t)&strftime_l_fake }, { "strtold_l", (uintptr_t)&strtold_l_fake },
  { "strtoll_l", (uintptr_t)&strtoll_l_fake }, { "strtoull_l", (uintptr_t)&strtoull_l_fake },
  { "wcscoll_l", (uintptr_t)&wcscoll_l_fake }, { "wcsxfrm_l", (uintptr_t)&wcsxfrm_l_fake },

  { "printf", (uintptr_t)&printf_fake }, { "puts", (uintptr_t)&puts },
  { "snprintf", (uintptr_t)&snprintf }, { "sprintf", (uintptr_t)&sprintf },
  { "swprintf", (uintptr_t)&swprintf }, { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf }, { "vasprintf", (uintptr_t)&vasprintf },
  { "sscanf", (uintptr_t)&sscanf }, { "vsscanf", (uintptr_t)&vsscanf },

  { "acosf", (uintptr_t)&acosf }, { "asinf", (uintptr_t)&asinf },
  { "atan2f", (uintptr_t)&atan2f }, { "cosf", (uintptr_t)&cosf },
  { "sinf", (uintptr_t)&sinf }, { "tanf", (uintptr_t)&tanf },
  { "expf", (uintptr_t)&expf }, { "logf", (uintptr_t)&logf },
  { "powf", (uintptr_t)&powf }, { "pow", (uintptr_t)&pow },
  { "fmodf", (uintptr_t)&fmodf }, { "sincosf", (uintptr_t)&sincosf_fake },

  { "clock_gettime", (uintptr_t)&clock_gettime }, { "gettimeofday", (uintptr_t)&gettimeofday },
  { "gmtime", (uintptr_t)&gmtime }, { "gmtime_r", (uintptr_t)&gmtime_r },
  { "localtime", (uintptr_t)&localtime }, { "localtime_r", (uintptr_t)&localtime_r },
  { "mktime", (uintptr_t)&mktime }, { "time", (uintptr_t)&time },
  { "nanosleep", (uintptr_t)&nanosleep }, { "usleep", (uintptr_t)&usleep },
  { "tzset", (uintptr_t)&tzset }, { "tzname", (uintptr_t)&g_tzname_fake },
  { "getenv", (uintptr_t)&getenv_fake }, { "putenv", (uintptr_t)&putenv },

  /* Fake __sF-aware stdio. */
  { "__sF", (uintptr_t)&fake_sF },
  { "stdin", (uintptr_t)&fake_sF[0] }, { "stdout", (uintptr_t)&fake_sF[1] }, { "stderr", (uintptr_t)&fake_sF[2] },
  { "fopen", (uintptr_t)&fopen_fake }, { "fclose", (uintptr_t)&fclose_fake },
  { "fread", (uintptr_t)&fread_fake }, { "fwrite", (uintptr_t)&fwrite_fake },
  { "fseek", (uintptr_t)&fseek_fake }, { "fseeko", (uintptr_t)&fseeko },
  { "ftell", (uintptr_t)&ftell_fake }, { "ftello", (uintptr_t)&ftello },
  { "fflush", (uintptr_t)&fflush_fake }, { "fprintf", (uintptr_t)&fprintf_fake },
  { "vfprintf", (uintptr_t)&vfprintf_fake }, { "fputc", (uintptr_t)&fputc_fake },
  { "fputs", (uintptr_t)&fputs_fake }, { "fgetc", (uintptr_t)&fgetc_fake },
  { "fgets", (uintptr_t)&fgets_fake }, { "getc", (uintptr_t)&getc_fake },
  { "getwc", (uintptr_t)&getc_fake }, { "fputwc", (uintptr_t)&fputc_fake },
  { "ungetc", (uintptr_t)&ungetc_fake }, { "ungetwc", (uintptr_t)&ungetc_fake },
  { "feof", (uintptr_t)&feof_fake }, { "ferror", (uintptr_t)&ferror_fake },
  { "fileno", (uintptr_t)&fileno_fake }, { "remove", (uintptr_t)&remove },
  { "rename", (uintptr_t)&rename },

  /* Filesystem. */
  { "open", (uintptr_t)&open_fake }, { "openat", (uintptr_t)&openat_fake },
  { "close", (uintptr_t)&close_fake }, { "read", (uintptr_t)&read_fake },
  { "write", (uintptr_t)&write_fake }, { "pwrite", (uintptr_t)&pwrite_impl },
  { "pread", (uintptr_t)&pread_impl },
  { "lseek", (uintptr_t)&z_lseek }, { "pipe", (uintptr_t)&pipe_fake },
  { "poll", (uintptr_t)&poll_fake }, { "select", (uintptr_t)&select_fake },
  { "dup2", (uintptr_t)&dup2_stub }, { "fcntl", (uintptr_t)&fcntl_fake },
  { "ioctl", (uintptr_t)&ioctl_fake }, { "isatty", (uintptr_t)&isatty },
  { "tcgetattr", (uintptr_t)&tcgetattr_stub }, { "tcsetattr", (uintptr_t)&tcsetattr_stub },
  { "stat", (uintptr_t)&stat_fake }, { "fstat", (uintptr_t)&fstat_fake },
  { "lstat", (uintptr_t)&lstat_fake }, { "statfs", (uintptr_t)&statfs_fake },
  { "statvfs", (uintptr_t)&statvfs_fake }, { "access", (uintptr_t)&access_fake },
  { "mkdir", (uintptr_t)&mkdir_fake }, { "rmdir", (uintptr_t)&rmdir },
  { "unlink", (uintptr_t)&unlink }, { "unlinkat", (uintptr_t)&unlinkat_fake },
  { "chdir", (uintptr_t)&chdir }, { "getcwd", (uintptr_t)&getcwd_fake },
  { "chmod", (uintptr_t)&chmod_stub }, { "fchmod", (uintptr_t)&fchmod_stub },
  { "fchmodat", (uintptr_t)&fchmodat_stub }, { "truncate", (uintptr_t)&truncate_impl },
  { "ftruncate", (uintptr_t)&ftruncate_impl }, { "fsync", (uintptr_t)&fsync_impl },
  { "link", (uintptr_t)&link_stub }, { "symlink", (uintptr_t)&symlink_stub },
  { "readlink", (uintptr_t)&readlink_stub }, { "utime", (uintptr_t)&ret0_i },
  { "utimensat", (uintptr_t)&utimensat_stub }, { "sendfile", (uintptr_t)&sendfile_impl },
  { "opendir", (uintptr_t)&opendir_fake }, { "closedir", (uintptr_t)&closedir },
  { "readdir", (uintptr_t)&readdir_fake }, { "fdopendir", (uintptr_t)&fdopendir_stub },
  { "realpath", (uintptr_t)&realpath_fake },
  { "strerror", (uintptr_t)&strerror }, { "strerror_r", (uintptr_t)&strerror_r_fake },

  { "signal", (uintptr_t)&signal_stub }, { "sigaction", (uintptr_t)&sigaction_stub },
  { "sigaddset", (uintptr_t)&ret0_i }, { "sigemptyset", (uintptr_t)&ret0_i },
  { "setjmp", (uintptr_t)&setjmp }, { "longjmp", (uintptr_t)&longjmp },
  { "siglongjmp", (uintptr_t)&longjmp },

  { "getpid", (uintptr_t)&getpid_fake }, { "getuid", (uintptr_t)&ret0_u },
  { "geteuid", (uintptr_t)&ret0_u }, { "getegid", (uintptr_t)&ret0_u },
  { "getpwuid", (uintptr_t)&getpwuid_fake }, { "getrusage", (uintptr_t)&getrusage_fake },
  { "fork", (uintptr_t)&fork_fake }, { "execvp", (uintptr_t)&execvp_fake },
  { "waitpid", (uintptr_t)&waitpid_fake }, { "kill", (uintptr_t)&kill_fake },
  { "sched_yield", (uintptr_t)&sched_yield_fake },
  { "sched_get_priority_max", (uintptr_t)&sched_get_priority_max_fake },
  { "sched_get_priority_min", (uintptr_t)&sched_get_priority_min_fake },

  { "dlopen", (uintptr_t)&dlopen_fake }, { "dlclose", (uintptr_t)&dlclose_fake },
  { "dlerror", (uintptr_t)&dlerror_fake }, { "dlsym", (uintptr_t)&dlsym_fake },

  /* Networking. */
  { "socket", (uintptr_t)&socket_fake }, { "connect", (uintptr_t)&connect_fake },
  { "bind", (uintptr_t)&bind_fake }, { "listen", (uintptr_t)&listen_fake },
  { "accept", (uintptr_t)&accept_fake }, { "send", (uintptr_t)&send_fake },
  { "recv", (uintptr_t)&recv_fake }, { "sendto", (uintptr_t)&sendto_fake },
  { "recvfrom", (uintptr_t)&recvfrom_fake }, { "shutdown", (uintptr_t)&shutdown_fake },
  { "setsockopt", (uintptr_t)&setsockopt_fake }, { "getsockopt", (uintptr_t)&getsockopt_fake },
  { "getsockname", (uintptr_t)&getsockname_fake }, { "getpeername", (uintptr_t)&getpeername_fake },
  { "getaddrinfo", (uintptr_t)&getaddrinfo_fake }, { "freeaddrinfo", (uintptr_t)&freeaddrinfo_fake },
  { "getnameinfo", (uintptr_t)&getnameinfo_fake }, { "gethostname", (uintptr_t)&gethostname_fake },
  { "getservbyname", (uintptr_t)&getservbyname_fake },
  { "if_nametoindex", (uintptr_t)&if_nametoindex_fake }, { "if_indextoname", (uintptr_t)&if_indextoname_fake },
  { "inet_aton", (uintptr_t)&inet_aton_fake }, { "inet_pton", (uintptr_t)&inet_pton_fake },
  { "inet_ntoa", (uintptr_t)&inet_ntoa_fake },

  /* Pthreads. */
  { "pthread_create", (uintptr_t)&pthread_create_fake }, { "pthread_join", (uintptr_t)&pthread_join_fake },
  { "pthread_detach", (uintptr_t)&pthread_detach }, { "pthread_exit", (uintptr_t)&pthread_exit },
  { "pthread_self", (uintptr_t)&pthread_self }, { "pthread_kill", (uintptr_t)&pthread_kill_gc },
  { "pthread_key_create", (uintptr_t)&pthread_key_create_fake }, { "pthread_key_delete", (uintptr_t)&pthread_key_delete_fake },
  { "pthread_getspecific", (uintptr_t)&pthread_getspecific_fake }, { "pthread_setspecific", (uintptr_t)&pthread_setspecific_fake },
  { "pthread_once", (uintptr_t)&pthread_once_fake },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake },
  { "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_fake },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake },
  { "pthread_mutex_timedlock", (uintptr_t)&pthread_mutex_timedlock_fake },
  { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_fake },
  { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_fake },
  { "pthread_mutexattr_destroy", (uintptr_t)&ret0_i },
  { "pthread_cond_init", (uintptr_t)&pthread_cond_init_fake },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake },
  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_fake },
  { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_fake },
  { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_fake },
  { "pthread_rwlock_init", (uintptr_t)&pthread_rwlock_init_fake },
  { "pthread_rwlock_destroy", (uintptr_t)&pthread_rwlock_destroy_fake },
  { "pthread_rwlock_rdlock", (uintptr_t)&pthread_rwlock_rdlock_fake },
  { "pthread_rwlock_wrlock", (uintptr_t)&pthread_rwlock_wrlock_fake },
  { "pthread_rwlock_unlock", (uintptr_t)&pthread_rwlock_unlock_fake },
  { "pthread_attr_init", (uintptr_t)&pthread_attr_init_fake },
  { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_fake },
  { "pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate_fake },
  { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize_fake },
  { "pthread_setschedparam", (uintptr_t)&pthread_setschedparam_fake },
  { "pthread_sigmask", (uintptr_t)&pthread_sigmask_fake },
  { "sem_init", (uintptr_t)&sem_init_fake }, { "sem_destroy", (uintptr_t)&sem_destroy_fake },
  { "sem_post", (uintptr_t)&sem_post_fake }, { "sem_wait", (uintptr_t)&sem_wait_fake },
  { "sem_getvalue", (uintptr_t)&sem_getvalue_fake },
  { "sem_trywait", (uintptr_t)&sem_trywait_fake },
  { "sem_timedwait", (uintptr_t)&sem_timedwait_fake },

  /* Mesa EGL and GLES. */
  { "eglGetDisplay", (uintptr_t)&eglGetDisplay }, { "eglInitialize", (uintptr_t)&eglInitialize },
  { "eglTerminate", (uintptr_t)&eglTerminate }, { "eglGetConfigs", (uintptr_t)&eglGetConfigs },
  { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
  { "eglCreateWindowSurface", (uintptr_t)&eglCreateWindowSurface },
  { "eglCreateContext", (uintptr_t)&eglCreateContext }, { "eglMakeCurrent", (uintptr_t)&eglMakeCurrent },
  { "eglSwapBuffers", (uintptr_t)&egl_SwapBuffers_present }, { "eglQuerySurface", (uintptr_t)&egl_QuerySurface },
  { "eglDestroyContext", (uintptr_t)&eglDestroyContext }, { "eglDestroySurface", (uintptr_t)&eglDestroySurface },

  { "glActiveTexture", (uintptr_t)&glActiveTexture }, { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindBuffer", (uintptr_t)&glBindBuffer }, { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer }, { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate }, { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBufferData", (uintptr_t)&glBufferData }, { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor }, { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil }, { "glColorMask", (uintptr_t)&glColorMask },
  { "glCompileShader", (uintptr_t)&glCompileShader }, { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
  { "glCreateProgram", (uintptr_t)&glCreateProgram }, { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCullFace", (uintptr_t)&glCullFace }, { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers }, { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers }, { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures }, { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask }, { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray }, { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements }, { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D }, { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers }, { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
  { "glGenTextures", (uintptr_t)&glGenTextures }, { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
  { "glGetError", (uintptr_t)&glGetError }, { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv }, { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glLinkProgram", (uintptr_t)&glLinkProgram }, { "glPixelStorei", (uintptr_t)&glPixelStorei },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffset }, { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage }, { "glScissor", (uintptr_t)&glScissor },
  { "glShaderSource", (uintptr_t)&glShaderSource }, { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilMask", (uintptr_t)&glStencilMask }, { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glTexImage2D", (uintptr_t)&glTexImage2D }, { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D }, { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i }, { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform3fv", (uintptr_t)&glUniform3fv }, { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv }, { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer }, { "glViewport", (uintptr_t)&glViewport },

  /* Android platform APIs. */
  { "ALooper_prepare", (uintptr_t)&ALooper_prepare }, { "ALooper_addFd", (uintptr_t)&ALooper_addFd },
  { "ALooper_pollOnce", (uintptr_t)&ALooper_pollOnce },
  { "AInputQueue_attachLooper", (uintptr_t)&AInputQueue_attachLooper },
  { "AInputQueue_detachLooper", (uintptr_t)&AInputQueue_detachLooper },
  { "AInputQueue_getEvent", (uintptr_t)&AInputQueue_getEvent },
  { "AInputQueue_preDispatchEvent", (uintptr_t)&AInputQueue_preDispatchEvent },
  { "AInputQueue_finishEvent", (uintptr_t)&AInputQueue_finishEvent },
  { "AInputEvent_getType", (uintptr_t)&AInputEvent_getType },
  { "AMotionEvent_getAction", (uintptr_t)&AMotionEvent_getAction },
  { "AMotionEvent_getPointerCount", (uintptr_t)&AMotionEvent_getPointerCount },
  { "AMotionEvent_getPointerId", (uintptr_t)&AMotionEvent_getPointerId },
  { "AMotionEvent_getX", (uintptr_t)&AMotionEvent_getX },
  { "AMotionEvent_getY", (uintptr_t)&AMotionEvent_getY },
  { "AKeyEvent_getKeyCode", (uintptr_t)&AKeyEvent_getKeyCode },
  { "AKeyEvent_getFlags", (uintptr_t)&AKeyEvent_getFlags },
  { "AKeyEvent_getRepeatCount", (uintptr_t)&AKeyEvent_getRepeatCount },
  { "ANativeWindow_setBuffersGeometry", (uintptr_t)&ANativeWindow_setBuffersGeometry },
  { "AConfiguration_new", (uintptr_t)&AConfiguration_new },
  { "AConfiguration_fromAssetManager", (uintptr_t)&AConfiguration_fromAssetManager },
  { "AConfiguration_getLanguage", (uintptr_t)&AConfiguration_getLanguage },
  { "AConfiguration_getCountry", (uintptr_t)&AConfiguration_getCountry },
  { "AConfiguration_delete", (uintptr_t)&AConfiguration_delete },
  { "ASensorEventQueue_enableSensor", (uintptr_t)&ASensorEventQueue_enableSensor },
  { "ASensorEventQueue_disableSensor", (uintptr_t)&ASensorEventQueue_disableSensor },
  { "ASensorEventQueue_setEventRate", (uintptr_t)&ASensorEventQueue_setEventRate },
  { "ASensorEventQueue_getEvents", (uintptr_t)&ASensorEventQueue_getEvents },

  { "AAssetManager_fromJava", (uintptr_t)&AAssetManager_fromJava },
  { "AAssetManager_open", (uintptr_t)&AAssetManager_open },
  { "AAsset_getBuffer", (uintptr_t)&AAsset_getBuffer },
  { "AAsset_getLength", (uintptr_t)&AAsset_getLength },
  { "AAsset_close", (uintptr_t)&AAsset_close },

  /* OpenSL ES. */
  { "__android_log_buf_write", (uintptr_t)&__android_log_buf_write },
  { "slCreateEngine", (uintptr_t)&slCreateEngine },
  #define SL_IID(n) { "SL_IID_" #n, (uintptr_t)&SL_IID_##n }
  SL_IID(3DCOMMIT), SL_IID(3DDOPPLER), SL_IID(3DGROUPING), SL_IID(3DLOCATION),
  SL_IID(3DMACROSCOPIC), SL_IID(3DSOURCE), SL_IID(ANDROIDCONFIGURATION),
  SL_IID(ANDROIDEFFECT), SL_IID(ANDROIDEFFECTCAPABILITIES), SL_IID(ANDROIDEFFECTSEND),
  SL_IID(ANDROIDSIMPLEBUFFERQUEUE), SL_IID(AUDIODECODERCAPABILITIES), SL_IID(AUDIOENCODER),
  SL_IID(AUDIOENCODERCAPABILITIES), SL_IID(AUDIOIODEVICECAPABILITIES), SL_IID(BASSBOOST),
  SL_IID(BUFFERQUEUE), SL_IID(DEVICEVOLUME), SL_IID(DYNAMICINTERFACEMANAGEMENT),
  SL_IID(DYNAMICSOURCE), SL_IID(EFFECTSEND), SL_IID(ENGINE), SL_IID(ENGINECAPABILITIES),
  SL_IID(ENVIRONMENTALREVERB), SL_IID(EQUALIZER), SL_IID(LED), SL_IID(METADATAEXTRACTION),
  SL_IID(METADATATRAVERSAL), SL_IID(MIDIMESSAGE), SL_IID(MIDIMUTESOLO), SL_IID(MIDITEMPO),
  SL_IID(MIDITIME), SL_IID(MUTESOLO), SL_IID(NULL), SL_IID(OBJECT), SL_IID(OUTPUTMIX),
  SL_IID(PITCH), SL_IID(PLAY), SL_IID(PLAYBACKRATE), SL_IID(PREFETCHSTATUS),
  SL_IID(PRESETREVERB), SL_IID(RATEPITCH), SL_IID(RECORD), SL_IID(SEEK), SL_IID(THREADSYNC),
  SL_IID(VIBRA), SL_IID(VIRTUALIZER), SL_IID(VISUALIZATION), SL_IID(VOLUME),
  #undef SL_IID
};

size_t dynlib_numfunctions = sizeof(dynlib_functions) / sizeof(*dynlib_functions);

/* Firebase libc imports. */
#include <wctype.h>
#define FBX_CT(n)  static int    fbx_##n(int c){return n(c);}
#define FBX_WCT(n) static int    fbx_##n(wint_t c){return n(c);}
FBX_CT(isalpha) FBX_CT(islower) FBX_CT(toupper)
FBX_WCT(iswalpha) FBX_WCT(iswblank) FBX_WCT(iswcntrl) FBX_WCT(iswdigit)
FBX_WCT(iswlower) FBX_WCT(iswprint) FBX_WCT(iswpunct) FBX_WCT(iswspace)
FBX_WCT(iswupper) FBX_WCT(iswxdigit) FBX_WCT(towupper)
static int    fbx_strcoll(const char*a,const char*b){return strcoll(a,b);}
static char  *fbx_strpbrk(const char*a,const char*b){return strpbrk(a,b);}
static size_t fbx_strxfrm(char*a,const char*b,size_t n){return strxfrm(a,b,n);}
static int    fbx_wcscoll(const wchar_t*a,const wchar_t*b){return wcscoll(a,b);}
static size_t fbx_wcsxfrm(wchar_t*a,const wchar_t*b,size_t n){return wcsxfrm(a,b,n);}
static int    fbx_inotify_init(void){errno=ENOSYS;return -1;}
static int    fbx_inotify_add_watch(int fd,const char*p,uint32_t m){(void)fd;(void)p;(void)m;errno=ENOSYS;return -1;}
static mode_t fbx_umask_chk(mode_t m){(void)m;return 0;} // no umask on Switch

/* Supplementary Unity and IL2CPP imports. */
static double vln_cosh(double x){ return cosh(x); }
static double vln_sinh(double x){ return sinh(x); }
static double vln_tanh(double x){ return tanh(x); }
static float  vln_nearbyintf(float x){ return nearbyintf(x); }
static void   vln_perror(const char*s){ (void)s; }
static char  *vln_strcasestr(const char*h,const char*n){
  if(!n||!*n) return (char*)h;
  for(; *h; h++){ const char*a=h,*b=n;
    while(*a && *b && tolower((unsigned char)*a)==tolower((unsigned char)*b)){a++;b++;}
    if(!*b) return (char*)h; }
  return NULL;
}
static int    vln_tcflush(int fd,int q){ (void)fd;(void)q; return 0; }      // no TTY on Switch
static int    vln_execl(const char*p,const char*a,...){ (void)p;(void)a; errno=ENOSYS; return -1; }
static const char *vln_gai_strerror(int e){ (void)e; return "getaddrinfo error"; }
/* AArch64 bionic already uses 64-bit off_t and stat layouts. */
static int acpc_check_vulkan_support(void) { return 0; }
DynLibFunction firebase_extra_functions[] = {
  { "isalpha",(uintptr_t)&fbx_isalpha },{ "islower",(uintptr_t)&fbx_islower },
  { "toupper",(uintptr_t)&fbx_toupper },
  { "iswalpha",(uintptr_t)&fbx_iswalpha },{ "iswblank",(uintptr_t)&fbx_iswblank },
  { "iswcntrl",(uintptr_t)&fbx_iswcntrl },{ "iswdigit",(uintptr_t)&fbx_iswdigit },
  { "iswlower",(uintptr_t)&fbx_iswlower },{ "iswprint",(uintptr_t)&fbx_iswprint },
  { "iswpunct",(uintptr_t)&fbx_iswpunct },{ "iswspace",(uintptr_t)&fbx_iswspace },
  { "iswupper",(uintptr_t)&fbx_iswupper },{ "iswxdigit",(uintptr_t)&fbx_iswxdigit },
  { "towupper",(uintptr_t)&fbx_towupper },
  { "strcoll",(uintptr_t)&fbx_strcoll },{ "strpbrk",(uintptr_t)&fbx_strpbrk },
  { "strxfrm",(uintptr_t)&fbx_strxfrm },{ "wcscoll",(uintptr_t)&fbx_wcscoll },
  { "wcsxfrm",(uintptr_t)&fbx_wcsxfrm },
  { "inotify_init",(uintptr_t)&fbx_inotify_init },
  { "inotify_add_watch",(uintptr_t)&fbx_inotify_add_watch },
  { "__umask_chk",(uintptr_t)&fbx_umask_chk },
  { "cosh",(uintptr_t)&vln_cosh },{ "sinh",(uintptr_t)&vln_sinh },{ "tanh",(uintptr_t)&vln_tanh },
  { "nearbyintf",(uintptr_t)&vln_nearbyintf },{ "perror",(uintptr_t)&vln_perror },
  { "strcasestr",(uintptr_t)&vln_strcasestr },{ "tcflush",(uintptr_t)&vln_tcflush },
  { "execl",(uintptr_t)&vln_execl },{ "gai_strerror",(uintptr_t)&vln_gai_strerror },
  { "fstat64",(uintptr_t)&fstat_fake },{ "stat64",(uintptr_t)&stat_fake },
  { "mmap64",(uintptr_t)&mmap_fake },
  /* libvulkanChecker selects the non-Vulkan path. */
  { "CheckVulkanSupport",(uintptr_t)&acpc_check_vulkan_support },
};
size_t firebase_extra_numfunctions = sizeof(firebase_extra_functions)/sizeof(*firebase_extra_functions);

/* Shared import table for relocation and dlsym(). */
static DynLibFunction *g_combined = NULL;
static int g_combined_n = 0;
static void build_combined(void) {
  if (g_combined) return;
  g_combined_n = (int)dynlib_numfunctions + unity_dynlib_numfunctions
               + (int)firebase_extra_numfunctions;
  g_combined = malloc((size_t)g_combined_n * sizeof(DynLibFunction));
  size_t off = 0;
  memcpy(g_combined + off, dynlib_functions, dynlib_numfunctions * sizeof(DynLibFunction));
  off += dynlib_numfunctions;
  memcpy(g_combined + off, unity_dynlib_functions,
         (size_t)unity_dynlib_numfunctions * sizeof(DynLibFunction));
  off += unity_dynlib_numfunctions;
  memcpy(g_combined + off, firebase_extra_functions,
         firebase_extra_numfunctions * sizeof(DynLibFunction));
}

/* Search the shim table for dlsym(). */
uintptr_t dynlib_find_export(const char *name) {
  if (!name) return 0;
  build_combined();
  for (int i = 0; i < g_combined_n; i++)
    if (strcmp(name, g_combined[i].symbol) == 0)
      return g_combined[i].func;
  return 0;
}

void resolve_imports(so_module *mod) {
  so_relocate(mod);
  build_combined();
  so_resolve(mod, g_combined, g_combined_n, 1);
}
