/* Supplementary Unity and IL2CPP imports. */
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <EGL/egl.h>
#include <zlib.h>
#include <switch.h>
#include <unistd.h>
#include "imports.h"
#include "libc_shim.h"

extern long z_lseek(int fd, long off, int whence);

extern void ALooper_acquire();
extern void ALooper_forThread();
extern void ALooper_pollOnce();
extern void ALooper_release();
extern void ALooper_wake();
extern void ANativeWindow_acquire();
extern void ANativeWindow_fromSurface();
extern void ANativeWindow_getHeight();
extern void ANativeWindow_getWidth();
extern void ANativeWindow_release();
extern void ASensorEventQueue_hasEvents();
extern void ASensorManager_createEventQueue();
extern void ASensorManager_destroyEventQueue();
extern void ASensorManager_getDefaultSensor();
extern void ASensorManager_getInstance();
extern void ASensorManager_getSensorList();
extern void ASensor_getMinDelay();
extern void ASensor_getName();
extern void ASensor_getResolution();
extern void ASensor_getType();
extern void ASensor_getVendor();

/* Unsupported Android media and camera APIs. */
#define Z_AMEDIA_ERROR_UNKNOWN (-10000)
static long z_media_fail(void) { return Z_AMEDIA_ERROR_UNKNOWN; }
static long z_media_zero(void) { return 0; }
static void *z_media_null(void) { return NULL; }
static void z_media_delete(void *p) { (void)p; }

typedef struct {
  uint32_t width, height, layers, format;
  uint64_t usage;
  uint32_t stride, rfu0;
  uint64_t rfu1;
} ZHardwareBufferDesc;
static void z_ahardwarebuffer_describe(const void *buffer, ZHardwareBufferDesc *desc) {
  (void)buffer;
  if (desc) memset(desc, 0, sizeof(*desc));
}
static long z_imagereader_new(int width, int height, int format, uint64_t usage,
                              int max_images, void **reader) {
  (void)width; (void)height; (void)format; (void)usage; (void)max_images;
  if (reader) *reader = NULL;
  return Z_AMEDIA_ERROR_UNKNOWN;
}
static long z_imagereader_get_window(void *reader, void **window) {
  (void)reader;
  if (window) *window = NULL;
  return Z_AMEDIA_ERROR_UNKNOWN;
}
static long z_imagereader_acquire(void *reader, void **image) {
  (void)reader;
  if (image) *image = NULL;
  return Z_AMEDIA_ERROR_UNKNOWN;
}
static long z_image_get_buffer(void *image, void **buffer) {
  (void)image;
  if (buffer) *buffer = NULL;
  return Z_AMEDIA_ERROR_UNKNOWN;
}
static long z_image_get_timestamp(void *image, int64_t *timestamp) {
  (void)image;
  if (timestamp) *timestamp = 0;
  return Z_AMEDIA_ERROR_UNKNOWN;
}
static void *z_nativewindow_to_surface(void *env, void *window) {
  (void)env;
  return window;
}
typedef struct { const void *iov_base; size_t iov_len; } ZIovec;
static long z_writev(int fd, const ZIovec *iov, int count) {
  long total = 0;
  if (!iov || count < 0) return -1;
  for (int i = 0; i < count; i++) {
    const unsigned char *p = (const unsigned char *)iov[i].iov_base;
    size_t left = iov[i].iov_len;
    while (left) {
      ssize_t n = write(fd, p, left);
      if (n <= 0) return total ? total : -1;
      total += n; p += n; left -= (size_t)n;
    }
  }
  return total;
}

/* NDK media-key pointer exports. */
static const char *z_media_key_channel_count = "channel-count";
static const char *z_media_key_color_format = "color-format";
static const char *z_media_key_color_range = "color-range";
static const char *z_media_key_color_standard = "color-standard";
static const char *z_media_key_duration = "durationUs";
static const char *z_media_key_encoder_delay = "encoder-delay";
static const char *z_media_key_frame_rate = "frame-rate";
static const char *z_media_key_height = "height";
static const char *z_media_key_language = "language";
static const char *z_media_key_mime = "mime";
static const char *z_media_key_rotation = "rotation-degrees";
static const char *z_media_key_sample_rate = "sample-rate";
static const char *z_media_key_slice_height = "slice-height";
static const char *z_media_key_stride = "stride";
static const char *z_media_key_width = "width";

/* Bionic _ctype_ table. */
char z_ctype[384];
__attribute__((constructor)) static void z_ctype_init(void){
  for(int c=0;c<256;c++){
    unsigned char f=0;
    if(isupper(c))  f|=0x01;
    if(islower(c))  f|=0x02;
    if(isdigit(c))  f|=0x04;
    if(isspace(c))  f|=0x08;
    if(ispunct(c))  f|=0x10;
    if(iscntrl(c))  f|=0x20;
    if(isxdigit(c)) f|=0x40;
    if(c==' ')      f|=0x80;
    z_ctype[c]=(char)f;
  }
}

static long z_stub0(void){ return 0; }

/* Bionic calls with output parameters. */
static int z_clock_getres(int clock_id, struct timespec *res) {
  (void)clock_id;
  if (res) { res->tv_sec = 0; res->tv_nsec = 1; }
  return 0;
}
static int z_pthread_condattr_init(void *attr) {
  if (attr) memset(attr, 0, sizeof(uint32_t));
  return 0;
}
static int z_pthread_condattr_destroy(void *attr) { (void)attr; return 0; }
static int z_pthread_condattr_setclock(void *attr, int clock_id) {
  (void)attr; (void)clock_id; return 0;
}
static int z_sched_getaffinity(int pid, size_t size, void *mask) {
  (void)pid;
  if (!mask || !size) { errno = EINVAL; return -1; }
  memset(mask, 0, size);
  ((unsigned char *)mask)[0] = 0x07;
  return 0;
}
static int z_sched_setaffinity(int pid, size_t size, const void *mask) {
  (void)pid; (void)size; (void)mask; return 0;
}
static int z_sigfillset(void *set) {
  if (!set) { errno = EINVAL; return -1; }
  *(uint64_t *)set = UINT64_MAX;
  return 0;
}
static int z_sigdelset(void *set, int sig) {
  if (!set || sig <= 0 || sig > 64) { errno = EINVAL; return -1; }
  *(uint64_t *)set &= ~(UINT64_C(1) << (sig - 1));
  return 0;
}
static int z_sigaltstack(const void *ss, void *old_ss) {
  (void)ss;
  if (old_ss) memset(old_ss, 0, 24);
  return 0;
}
static int z_sigsuspend(const void *mask) {
  (void)mask; errno = EINTR; return -1;
}

typedef struct {
  char *pw_name, *pw_passwd;
  uint32_t pw_uid, pw_gid;
  char *pw_gecos, *pw_dir, *pw_shell;
} ZBionicPasswd;
static int z_getpwuid_r(uint32_t uid, ZBionicPasswd *pwd, char *buf,
                        size_t buflen, ZBionicPasswd **result) {
  static const char packed[] = "switch\0\0\0/switch/acpc_nx\0/bin/sh\0";
  if (result) *result = NULL;
  if (!pwd || !buf || buflen < sizeof packed) return ERANGE;
  memcpy(buf, packed, sizeof packed);
  char *p = buf;
  pwd->pw_name = p; p += strlen(p) + 1;
  pwd->pw_passwd = p; p += strlen(p) + 1;
  pwd->pw_gecos = p; p += strlen(p) + 1;
  pwd->pw_dir = p; p += strlen(p) + 1;
  pwd->pw_shell = p;
  pwd->pw_uid = uid; pwd->pw_gid = 0;
  if (result) *result = pwd;
  return 0;
}

static uint64_t z_rand48_state = UINT64_C(0x1234abcd330e);
static void z_srand48(long seed) {
  z_rand48_state = (((uint64_t)(uint32_t)seed) << 16) | UINT64_C(0x330e);
}
static long z_lrand48(void) {
  z_rand48_state = (z_rand48_state * UINT64_C(0x5deece66d) + 0xb) & UINT64_C(0xffffffffffff);
  return (long)(z_rand48_state >> 17);
}

static long z_prctl(int option, unsigned long a2, unsigned long a3,
                    unsigned long a4, unsigned long a5){
  (void)option; (void)a2; (void)a3; (void)a4; (void)a5;
  return 0;
}
static int z_pthread_setname_np(void *thread, const char *name){
  (void)thread; (void)name;
  return 0;
}

/* Return mapped thread-stack bounds for the Boehm GC. */
int z_pthread_attr_getstack(const void *attr, void **stackaddr, size_t *stacksize){
  (void)attr;
  uintptr_t sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
  void *base; size_t sz;
  MemoryInfo mi; u32 pi;
  if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, (u64)sp)) && mi.size){
    base = (void *)(uintptr_t)mi.addr;
    sz   = (size_t)mi.size;
  } else {
    base = (void *)(sp & ~0xFFFFFull);
    sz   = 0x100000;
  }
  if (stackaddr) *stackaddr = base;
  if (stacksize) *stacksize = sz;
  return 0;
}
int z_pthread_getattr_np(void *thread, void *attr){ (void)thread; (void)attr; return 0; }

int z_getpagesize(void){ return 0x1000; }
unsigned z_getgid(void){ return 0; }
int z_mlock(const void *addr, size_t len){ (void)addr; (void)len; return 0; }
int z_sigprocmask(int how, const void *set, void *oldset){
  (void)how; (void)set;
  if (oldset) memset(oldset, 0, sizeof(uint64_t));
  return 0;
}
int z_pthread_equal(unsigned long a, unsigned long b){ return a==b; }
int z_gettid(void){ return gettid_fake(); }
int z_dup(int fd){ return dup(fd); }
/* JNI may supply NULL device strings; treat them as empty. */
int z_strcasecmp(const char *a, const char *b){
  if (a == b) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return strcasecmp(a, b);
}
int z_strncasecmp(const char *a, const char *b, unsigned long n){
  if (a == b || n == 0) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return strncasecmp(a, b, n);
}
/* GNU-style basename without modifying the input. */
char *z_basename(const char *path){
  if (!path || !*path) return (char *)".";
  const char *s = strrchr(path, '/');
  return (char *)(s ? s + 1 : path);
}
/* Locale-aware ctype delegates to the current process locale. */
int z_isdigit_l (int c, void *l){ (void)l; return isdigit(c); }
int z_islower_l (int c, void *l){ (void)l; return islower(c); }
int z_isupper_l (int c, void *l){ (void)l; return isupper(c); }
int z_isxdigit_l(int c, void *l){ (void)l; return isxdigit(c); }
int z_tolower_l (int c, void *l){ (void)l; return tolower(c); }
int z_toupper_l (int c, void *l){ (void)l; return toupper(c); }
void z_sincos(double x,double*s,double*c){ if(s)*s=sin(x); if(c)*c=cos(x); }
void*z_memrchr(const void*s,int c,unsigned long n){ const unsigned char*p=(const unsigned char*)s+n; while(n--){ if(*--p==(unsigned char)c) return (void*)p; } return 0; }

DynLibFunction unity_dynlib_functions[] = {
  { "ALooper_acquire", (uintptr_t)&ALooper_acquire },
  { "ALooper_forThread", (uintptr_t)&ALooper_forThread },
  { "ALooper_pollAll", (uintptr_t)&ALooper_pollOnce },
  { "ALooper_release", (uintptr_t)&ALooper_release },
  { "ALooper_wake", (uintptr_t)&ALooper_wake },
  { "ANativeWindow_acquire", (uintptr_t)&ANativeWindow_acquire },
  { "ANativeWindow_fromSurface", (uintptr_t)&ANativeWindow_fromSurface },
  { "ANativeWindow_getHeight", (uintptr_t)&ANativeWindow_getHeight },
  { "ANativeWindow_getWidth", (uintptr_t)&ANativeWindow_getWidth },
  { "ANativeWindow_release", (uintptr_t)&ANativeWindow_release },
  { "ANativeWindow_toSurface", (uintptr_t)&z_nativewindow_to_surface },
  { "AHardwareBuffer_acquire", (uintptr_t)&z_media_delete },
  { "AHardwareBuffer_describe", (uintptr_t)&z_ahardwarebuffer_describe },
  { "AHardwareBuffer_release", (uintptr_t)&z_media_delete },
  { "AImageReader_acquireLatestImage", (uintptr_t)&z_imagereader_acquire },
  { "AImageReader_delete", (uintptr_t)&z_media_delete },
  { "AImageReader_getWindow", (uintptr_t)&z_imagereader_get_window },
  { "AImageReader_newWithUsage", (uintptr_t)&z_imagereader_new },
  { "AImageReader_setBufferRemovedListener", (uintptr_t)&z_media_fail },
  { "AImageReader_setImageListener", (uintptr_t)&z_media_fail },
  { "AImage_delete", (uintptr_t)&z_media_delete },
  { "AImage_deleteAsync", (uintptr_t)&z_media_delete },
  { "AImage_getHardwareBuffer", (uintptr_t)&z_image_get_buffer },
  { "AImage_getTimestamp", (uintptr_t)&z_image_get_timestamp },
  { "AMEDIAFORMAT_KEY_CHANNEL_COUNT", (uintptr_t)&z_media_key_channel_count },
  { "AMEDIAFORMAT_KEY_COLOR_FORMAT", (uintptr_t)&z_media_key_color_format },
  { "AMEDIAFORMAT_KEY_COLOR_RANGE", (uintptr_t)&z_media_key_color_range },
  { "AMEDIAFORMAT_KEY_COLOR_STANDARD", (uintptr_t)&z_media_key_color_standard },
  { "AMEDIAFORMAT_KEY_DURATION", (uintptr_t)&z_media_key_duration },
  { "AMEDIAFORMAT_KEY_ENCODER_DELAY", (uintptr_t)&z_media_key_encoder_delay },
  { "AMEDIAFORMAT_KEY_FRAME_RATE", (uintptr_t)&z_media_key_frame_rate },
  { "AMEDIAFORMAT_KEY_HEIGHT", (uintptr_t)&z_media_key_height },
  { "AMEDIAFORMAT_KEY_LANGUAGE", (uintptr_t)&z_media_key_language },
  { "AMEDIAFORMAT_KEY_MIME", (uintptr_t)&z_media_key_mime },
  { "AMEDIAFORMAT_KEY_ROTATION", (uintptr_t)&z_media_key_rotation },
  { "AMEDIAFORMAT_KEY_SAMPLE_RATE", (uintptr_t)&z_media_key_sample_rate },
  { "AMEDIAFORMAT_KEY_SLICE_HEIGHT", (uintptr_t)&z_media_key_slice_height },
  { "AMEDIAFORMAT_KEY_STRIDE", (uintptr_t)&z_media_key_stride },
  { "AMEDIAFORMAT_KEY_WIDTH", (uintptr_t)&z_media_key_width },
  { "AMediaCodec_configure", (uintptr_t)&z_media_fail },
  { "AMediaCodec_createDecoderByType", (uintptr_t)&z_media_null },
  { "AMediaCodec_delete", (uintptr_t)&z_media_delete },
  { "AMediaCodec_dequeueInputBuffer", (uintptr_t)&z_media_fail },
  { "AMediaCodec_dequeueOutputBuffer", (uintptr_t)&z_media_fail },
  { "AMediaCodec_flush", (uintptr_t)&z_media_fail },
  { "AMediaCodec_getInputBuffer", (uintptr_t)&z_media_null },
  { "AMediaCodec_getOutputBuffer", (uintptr_t)&z_media_null },
  { "AMediaCodec_getOutputFormat", (uintptr_t)&z_media_null },
  { "AMediaCodec_queueInputBuffer", (uintptr_t)&z_media_fail },
  { "AMediaCodec_releaseOutputBuffer", (uintptr_t)&z_media_fail },
  { "AMediaCodec_setOutputSurface", (uintptr_t)&z_media_fail },
  { "AMediaCodec_start", (uintptr_t)&z_media_fail },
  { "AMediaCodec_stop", (uintptr_t)&z_media_fail },
  { "AMediaDataSource_delete", (uintptr_t)&z_media_delete },
  { "AMediaDataSource_new", (uintptr_t)&z_media_null },
  { "AMediaDataSource_setClose", (uintptr_t)&z_media_zero },
  { "AMediaDataSource_setGetSize", (uintptr_t)&z_media_zero },
  { "AMediaDataSource_setReadAt", (uintptr_t)&z_media_zero },
  { "AMediaDataSource_setUserdata", (uintptr_t)&z_media_zero },
  { "AMediaExtractor_advance", (uintptr_t)&z_media_zero },
  { "AMediaExtractor_delete", (uintptr_t)&z_media_delete },
  { "AMediaExtractor_getSampleTime", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_getSampleTrackIndex", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_getTrackCount", (uintptr_t)&z_media_zero },
  { "AMediaExtractor_getTrackFormat", (uintptr_t)&z_media_null },
  { "AMediaExtractor_new", (uintptr_t)&z_media_null },
  { "AMediaExtractor_readSampleData", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_seekTo", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_selectTrack", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_setDataSource", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_setDataSourceCustom", (uintptr_t)&z_media_fail },
  { "AMediaExtractor_setDataSourceFd", (uintptr_t)&z_media_fail },
  { "AMediaFormat_delete", (uintptr_t)&z_media_delete },
  { "AMediaFormat_getFloat", (uintptr_t)&z_media_zero },
  { "AMediaFormat_getInt32", (uintptr_t)&z_media_zero },
  { "AMediaFormat_getInt64", (uintptr_t)&z_media_zero },
  { "AMediaFormat_getString", (uintptr_t)&z_media_zero },
  { "AMediaFormat_setInt32", (uintptr_t)&z_media_zero },
  { "ASensorEventQueue_hasEvents", (uintptr_t)&ASensorEventQueue_hasEvents },
  { "ASensorManager_createEventQueue", (uintptr_t)&ASensorManager_createEventQueue },
  { "ASensorManager_destroyEventQueue", (uintptr_t)&ASensorManager_destroyEventQueue },
  { "ASensorManager_getDefaultSensor", (uintptr_t)&ASensorManager_getDefaultSensor },
  { "ASensorManager_getInstance", (uintptr_t)&ASensorManager_getInstance },
  { "ASensorManager_getSensorList", (uintptr_t)&ASensorManager_getSensorList },
  { "ASensor_getMinDelay", (uintptr_t)&ASensor_getMinDelay },
  { "ASensor_getName", (uintptr_t)&ASensor_getName },
  { "ASensor_getResolution", (uintptr_t)&ASensor_getResolution },
  { "ASensor_getType", (uintptr_t)&ASensor_getType },
  { "ASensor_getVendor", (uintptr_t)&ASensor_getVendor },
  { "_ZTH15gDeferredAction", (uintptr_t)&z_stub0 },
  { "__system_property_find", (uintptr_t)&z_stub0 },
  { "__system_property_read", (uintptr_t)&z_stub0 },
  { "_ctype_", (uintptr_t)&z_ctype[0] },
  { "acos", (uintptr_t)&acos },
  { "asin", (uintptr_t)&asin },
  { "atan", (uintptr_t)&atan },
  { "atan2", (uintptr_t)&atan2 },
  { "atanf", (uintptr_t)&atanf },
  { "atol", (uintptr_t)&atol },
  { "basename", (uintptr_t)&z_basename },
  { "bsearch", (uintptr_t)&bsearch },
  { "cbrtf", (uintptr_t)&cbrtf },
  { "clearerr", (uintptr_t)&clearerr },
  { "clock", (uintptr_t)&clock },
  { "clock_getres", (uintptr_t)&z_clock_getres },
  { "cos", (uintptr_t)&cos },
  { "difftime", (uintptr_t)&difftime },
  { "div", (uintptr_t)&div },
  { "deflate", (uintptr_t)&deflate },
  { "deflateEnd", (uintptr_t)&deflateEnd },
  { "deflateInit_", (uintptr_t)&deflateInit_ },
  { "dladdr", (uintptr_t)&z_stub0 },
  { "dup", (uintptr_t)&z_dup },
  { "eglChooseConfig", (uintptr_t)&eglChooseConfig },
  { "eglCreatePbufferSurface", (uintptr_t)&eglCreatePbufferSurface },
  { "eglGetCurrentContext", (uintptr_t)&eglGetCurrentContext },
  { "eglGetCurrentSurface", (uintptr_t)&eglGetCurrentSurface },
  { "eglGetError", (uintptr_t)&eglGetError },
  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
  { "eglQueryString", (uintptr_t)&eglQueryString },
  { "eglSurfaceAttrib", (uintptr_t)&eglSurfaceAttrib },
  { "eglSwapInterval", (uintptr_t)&eglSwapInterval },
  { "exit", (uintptr_t)&exit_fake },
  { "exp", (uintptr_t)&exp },
  { "exp2f", (uintptr_t)&exp2f },
  { "fdopen", (uintptr_t)&fdopen },
  { "flock", (uintptr_t)&z_stub0 },
  { "fmod", (uintptr_t)&fmod },
  { "fnmatch", (uintptr_t)&z_stub0 },
  { "fscanf", (uintptr_t)&fscanf },
  { "futimens", (uintptr_t)&z_stub0 },
  { "gethostbyaddr", (uintptr_t)&z_stub0 },
  { "gethostbyname", (uintptr_t)&z_stub0 },
  { "getgid", (uintptr_t)&z_getgid },
  { "getpagesize", (uintptr_t)&z_getpagesize },
  { "getpriority", (uintptr_t)&z_stub0 },
  { "getpwuid_r", (uintptr_t)&z_getpwuid_r },
  { "gettid", (uintptr_t)&z_gettid },
  { "hypot", (uintptr_t)&hypot },
  { "inet_addr", (uintptr_t)&inet_addr_fake },
  { "inet_ntop", (uintptr_t)&inet_ntop_fake },
  { "inflate", (uintptr_t)&inflate },
  { "inflateEnd", (uintptr_t)&inflateEnd },
  { "inflateInit2_", (uintptr_t)&inflateInit2_ },
  { "inflateInit_", (uintptr_t)&inflateInit_ },
  { "isdigit_l", (uintptr_t)&z_isdigit_l },
  { "islower_l", (uintptr_t)&z_islower_l },
  { "isupper_l", (uintptr_t)&z_isupper_l },
  { "isxdigit_l", (uintptr_t)&z_isxdigit_l },
  { "ldexp", (uintptr_t)&ldexp },
  { "ldexpf", (uintptr_t)&ldexpf },
  { "lldiv", (uintptr_t)&lldiv },
  { "log", (uintptr_t)&log },
  { "log10", (uintptr_t)&log10 },
  { "log10f", (uintptr_t)&log10f },
  { "log2", (uintptr_t)&log2 },
  { "log2f", (uintptr_t)&log2f },
  { "logb", (uintptr_t)&logb },
  { "lrand48", (uintptr_t)&z_lrand48 },
  { "lseek64", (uintptr_t)&z_lseek },
  { "madvise", (uintptr_t)&z_stub0 },
  { "mlock", (uintptr_t)&z_mlock },
  { "memrchr", (uintptr_t)&z_memrchr },
  { "modf", (uintptr_t)&modf },
  { "modff", (uintptr_t)&modff },
  { "prctl", (uintptr_t)&z_prctl },
  { "pthread_atfork", (uintptr_t)&z_stub0 },
  { "pthread_attr_getstack", (uintptr_t)&z_pthread_attr_getstack },
  { "pthread_condattr_destroy", (uintptr_t)&z_pthread_condattr_destroy },
  { "pthread_condattr_init", (uintptr_t)&z_pthread_condattr_init },
  { "pthread_condattr_setclock", (uintptr_t)&z_pthread_condattr_setclock },
  { "pthread_equal", (uintptr_t)&z_pthread_equal },
  { "pthread_getattr_np", (uintptr_t)&z_pthread_getattr_np },
  { "pthread_rwlock_init", (uintptr_t)&pthread_rwlock_init_fake },
  { "pthread_rwlock_destroy", (uintptr_t)&pthread_rwlock_destroy_fake },
  { "pthread_setname_np", (uintptr_t)&z_pthread_setname_np },
  { "ptrace", (uintptr_t)&z_stub0 },
  { "raise", (uintptr_t)&raise_fake },
  { "recvmsg", (uintptr_t)&z_stub0 },
  { "scalbn", (uintptr_t)&scalbn },
  { "sched_getaffinity", (uintptr_t)&z_sched_getaffinity },
  { "sched_setaffinity", (uintptr_t)&z_sched_setaffinity },
  { "sem_getvalue", (uintptr_t)&z_stub0 },
  { "sendmsg", (uintptr_t)&z_stub0 },
  { "setbuf", (uintptr_t)&setbuf },
  { "setenv", (uintptr_t)&setenv },
  { "setpriority", (uintptr_t)&z_stub0 },
  { "setvbuf", (uintptr_t)&setvbuf },
  { "sigaltstack", (uintptr_t)&z_sigaltstack },
  { "sigdelset", (uintptr_t)&z_sigdelset },
  { "sigfillset", (uintptr_t)&z_sigfillset },
  { "sigprocmask", (uintptr_t)&z_sigprocmask },
  { "sigsetjmp", (uintptr_t)&setjmp },
  { "sigsuspend", (uintptr_t)&z_sigsuspend },
  { "sin", (uintptr_t)&sin },
  { "sincos", (uintptr_t)&z_sincos },
  { "sqrtf", (uintptr_t)&sqrtf },
  { "srand48", (uintptr_t)&z_srand48 },
  { "strcasecmp", (uintptr_t)&z_strcasecmp },
  { "strcspn", (uintptr_t)&strcspn },
  { "strdup", (uintptr_t)&strdup },
  { "strftime", (uintptr_t)&strftime },
  { "strlcpy", (uintptr_t)&strlcpy },
  { "strnlen", (uintptr_t)&strnlen },
  { "strspn", (uintptr_t)&strspn },
  { "strtok_r", (uintptr_t)&strtok_r },
  { "tan", (uintptr_t)&tan },
  { "tolower_l", (uintptr_t)&z_tolower_l },
  { "toupper_l", (uintptr_t)&z_toupper_l },
  { "towlower", (uintptr_t)&towlower },
  { "unsetenv", (uintptr_t)&unsetenv },
  { "utimes", (uintptr_t)&z_stub0 },
  { "vprintf", (uintptr_t)&vprintf },
  { "wmemcpy", (uintptr_t)&wmemcpy },
  { "wmemmove", (uintptr_t)&wmemmove },
  { "wmemset", (uintptr_t)&wmemset },
  { "writev", (uintptr_t)&z_writev },
  { "zError", (uintptr_t)&zError },
};
int unity_dynlib_numfunctions = (int)(sizeof(unity_dynlib_functions)/sizeof(unity_dynlib_functions[0]));
