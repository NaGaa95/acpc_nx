/* Unity NDK shim declarations. */
#ifndef ANDROID_NATIVE_UNITY_H
#define ANDROID_NATIVE_UNITY_H

#include <stdint.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ALooper       ALooper;

void  android_native_input_init(void);
void  android_native_update_mode(void);
void  android_native_draw_cursor(void);
void  android_native_feed_hid(uint8_t (*inject)(void*,void*,void*,int),
                              void *env, void *thiz);

void     ANativeWindow_acquire(ANativeWindow *);
void     ANativeWindow_release(ANativeWindow *);
ANativeWindow *ANativeWindow_fromSurface(void *, void *);
int32_t  ANativeWindow_getWidth(ANativeWindow *);
int32_t  ANativeWindow_getHeight(ANativeWindow *);
int32_t  ANativeWindow_setBuffersGeometry(ANativeWindow *, int32_t, int32_t, int32_t);

ALooper *ALooper_prepare(int);
ALooper *ALooper_forThread(void);
void     ALooper_acquire(ALooper *);
void     ALooper_release(ALooper *);
void     ALooper_wake(ALooper *);
int      ALooper_pollOnce(int, int *, int *, void **);
int      ALooper_addFd(ALooper *, int, int, int, void *, void *);

void *ASensorManager_getInstance(void);
int   ASensorManager_getSensorList(void *, void **);
void *ASensorManager_getDefaultSensor(void *, int);
void *ASensorManager_createEventQueue(void *, void *, int, void *, void *);
int   ASensorManager_destroyEventQueue(void *, void *);
int   ASensorEventQueue_enableSensor(void *, const void *);
int   ASensorEventQueue_disableSensor(void *, const void *);
int   ASensorEventQueue_setEventRate(void *, const void *, int32_t);
int   ASensorEventQueue_getEvents(void *, void *, size_t);
int   ASensorEventQueue_hasEvents(void *);
const char *ASensor_getName(const void *);
const char *ASensor_getVendor(const void *);
int         ASensor_getType(const void *);
float       ASensor_getResolution(const void *);
int         ASensor_getMinDelay(const void *);

void android_get_orientation(float *x, float *y, float *z);

int  fakefd_is_fake(int fd);
int  fakefd_pipe(int fds[2]);
long fakefd_read(int fd, void *buf, unsigned long n);
long fakefd_write(int fd, const void *buf, unsigned long n);
int  fakefd_close(int fd);

#endif
