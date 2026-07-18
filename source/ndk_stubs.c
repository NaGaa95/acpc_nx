/* Android NDK shims required by Unity. */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

const char *data_dir(void) { return GAME_HOME; }

/* AInputQueue / AInputEvent / AMotionEvent / AKeyEvent */
void    AInputQueue_attachLooper(void *a, void *b, int c, void *d, void *e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void    AInputQueue_detachLooper(void *a) { (void)a; }
int32_t AInputQueue_getEvent(void *a, void **b) { (void)a;(void)b; return -1; }   /* no events */
int32_t AInputQueue_preDispatchEvent(void *a, void *b) { (void)a;(void)b; return 0; }
void    AInputQueue_finishEvent(void *a, void *b, int c) { (void)a;(void)b;(void)c; }
int32_t AInputEvent_getType(const void *a) { (void)a; return 0; }
int32_t AMotionEvent_getAction(const void *a) { (void)a; return 0; }
size_t  AMotionEvent_getPointerCount(const void *a) { (void)a; return 0; }
int32_t AMotionEvent_getPointerId(const void *a, size_t b) { (void)a;(void)b; return 0; }
float   AMotionEvent_getX(const void *a, size_t b) { (void)a;(void)b; return 0.0f; }
float   AMotionEvent_getY(const void *a, size_t b) { (void)a;(void)b; return 0.0f; }
int32_t AKeyEvent_getKeyCode(const void *a) { (void)a; return 0; }
int32_t AKeyEvent_getFlags(const void *a) { (void)a; return 0; }
int32_t AKeyEvent_getRepeatCount(const void *a) { (void)a; return 0; }

/* AConfiguration */
void *AConfiguration_new(void) { return calloc(1, 1); }
void  AConfiguration_fromAssetManager(void *a, void *b) { (void)a;(void)b; }
void  AConfiguration_getLanguage(void *a, char *out) { (void)a; if (out) { out[0]='e'; out[1]='n'; } }
void  AConfiguration_getCountry(void *a, char *out) { (void)a; if (out) { out[0]='U'; out[1]='S'; } }
void  AConfiguration_delete(void *a) { free(a); }

/* AAsset over the staged loose `assets/` tree. Newer Unity versions still use
 * the NDK API for a few bootstrap/streaming reads even though most calls go
 * through the Java AssetManager shim. */
#define NX_ASSET_MAGIC 0x41534350u /* "ASCP" */
typedef struct {
  uint32_t magic;
  unsigned char *data;
  size_t size;
  size_t pos;
} NxAsset;
static int g_asset_manager_token;

void *AAssetManager_fromJava(void *env, void *manager) {
  (void)env; (void)manager;
  return &g_asset_manager_token;
}

void *AAssetManager_open(void *manager, const char *name, int mode) {
  (void)manager; (void)mode;
  if (!name || strstr(name, "..")) return NULL;
  while (*name == '/') name++;
  if (!strncmp(name, "assets/", 7)) name += 7;
  char path[768];
  snprintf(path, sizeof path, "%s/assets/%s", GAME_HOME, name);
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
  long end = ftell(fp);
  if (end < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
  NxAsset *asset = calloc(1, sizeof(*asset));
  if (!asset) { fclose(fp); return NULL; }
  asset->data = malloc(end > 0 ? (size_t)end : 1);
  if (!asset->data || (end > 0 && fread(asset->data, (size_t)end, 1, fp) != 1)) {
    fclose(fp); free(asset->data); free(asset); return NULL;
  }
  fclose(fp);
  asset->magic = NX_ASSET_MAGIC;
  asset->size = (size_t)end;
  return asset;
}

static NxAsset *nx_asset(void *p) {
  NxAsset *a = p;
  return a && a->magic == NX_ASSET_MAGIC ? a : NULL;
}
const void *AAsset_getBuffer(void *p) { NxAsset *a = nx_asset(p); return a ? a->data : NULL; }
int64_t AAsset_getLength(void *p) { NxAsset *a = nx_asset(p); return a ? (int64_t)a->size : 0; }
void AAsset_close(void *p) {
  NxAsset *a = nx_asset(p);
  if (!a) return;
  a->magic = 0; free(a->data); free(a);
}

