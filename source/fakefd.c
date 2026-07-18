/* In-process file descriptors for Unity pipe signaling. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <switch.h>

#define FAKE_FD_BASE 0x40000000
#define MAX_FAKE_FDS 32
#define PIPE_CAP     4096

enum { FD_NONE = 0, FD_PIPE_R, FD_PIPE_W };

typedef struct { uint8_t buf[PIPE_CAP]; size_t head, len; int refs; } Pipe;
typedef struct { int kind; Pipe *pipe; } FakeFd;

static FakeFd  g_fds[MAX_FAKE_FDS];
static Mutex   g_lock;
static CondVar g_cond;
static int     g_inited = 0;

static void fakefd_once(void) {
  if (!g_inited) { mutexInit(&g_lock); condvarInit(&g_cond); g_inited = 1; }
}
static int alloc_slot(void) {
  for (int i = 0; i < MAX_FAKE_FDS; i++)
    if (g_fds[i].kind == FD_NONE) return i;
  return -1;
}

int fakefd_is_fake(int fd) {
  return fd >= FAKE_FD_BASE && fd < FAKE_FD_BASE + MAX_FAKE_FDS;
}

int fakefd_pipe(int fds[2]) {
  fakefd_once();
  mutexLock(&g_lock);
  int r = alloc_slot();
  if (r < 0) { mutexUnlock(&g_lock); return -1; }
  g_fds[r].kind = FD_PIPE_R;
  int w = alloc_slot();
  if (w < 0) { g_fds[r].kind = FD_NONE; mutexUnlock(&g_lock); return -1; }
  g_fds[w].kind = FD_PIPE_W;
  Pipe *p = calloc(1, sizeof(*p));
  if (!p) { g_fds[r].kind = g_fds[w].kind = FD_NONE; mutexUnlock(&g_lock); return -1; }
  p->refs = 2;
  g_fds[r].pipe = g_fds[w].pipe = p;
  mutexUnlock(&g_lock);
  fds[0] = FAKE_FD_BASE + r;
  fds[1] = FAKE_FD_BASE + w;
  return 0;
}

long fakefd_write(int fd, const void *buf, unsigned long n) {
  const int slot = fd - FAKE_FD_BASE;
  if (slot < 0 || slot >= MAX_FAKE_FDS) return -1;
  fakefd_once();
  mutexLock(&g_lock);
  Pipe *p = g_fds[slot].pipe;
  if (g_fds[slot].kind != FD_PIPE_W || !p) { mutexUnlock(&g_lock); return -1; }
  size_t wrote = 0;
  const uint8_t *src = buf;
  while (wrote < n && p->len < PIPE_CAP) {
    const size_t tail = (p->head + p->len) % PIPE_CAP;
    p->buf[tail] = src[wrote++];
    p->len++;
  }
  condvarWakeAll(&g_cond);
  mutexUnlock(&g_lock);
  return (long)wrote;
}

long fakefd_read(int fd, void *buf, unsigned long n) {
  const int slot = fd - FAKE_FD_BASE;
  if (slot < 0 || slot >= MAX_FAKE_FDS) return -1;
  fakefd_once();
  mutexLock(&g_lock);
  Pipe *p = g_fds[slot].pipe;
  if (g_fds[slot].kind != FD_PIPE_R || !p) { mutexUnlock(&g_lock); return -1; }
  /* block until data arrives or all writers close (refs drops to the reader) */
  while (p->len == 0 && p->refs > 1) condvarWait(&g_cond, &g_lock);
  size_t got = 0;
  uint8_t *dst = buf;
  while (got < n && p->len > 0) {
    dst[got++] = p->buf[p->head];
    p->head = (p->head + 1) % PIPE_CAP;
    p->len--;
  }
  mutexUnlock(&g_lock);
  return (long)got;            /* 0 == EOF (writers gone, buffer drained) */
}

int fakefd_close(int fd) {
  const int slot = fd - FAKE_FD_BASE;
  if (slot < 0 || slot >= MAX_FAKE_FDS) return -1;
  fakefd_once();
  mutexLock(&g_lock);
  FakeFd *f = &g_fds[slot];
  if (f->pipe && --f->pipe->refs <= 0) free(f->pipe);
  memset(f, 0, sizeof(*f));
  condvarWakeAll(&g_cond);      /* wake any blocked reader -> treats as EOF */
  mutexUnlock(&g_lock);
  return 0;
}
