#ifndef __DATA_H__
#define __DATA_H__

#include <stdint.h>

const char *data_dir(void);
void *AAssetManager_fromJava(void *env, void *assetManager);
void *AAssetManager_open(void *mgr, const char *filename, int mode);
const void *AAsset_getBuffer(void *asset);
int64_t AAsset_getLength(void *asset);
void AAsset_close(void *asset);

#endif
