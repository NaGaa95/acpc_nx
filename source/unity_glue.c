/* Unity host object initialization. */

#include "unity_jni.h"

void *fake_unityplayer_thiz = 0;
void *fake_context_obj      = 0;
void *fake_surface_obj      = 0;

void unity_environment_init(const char *data_root)
{
  unity_jni_init(data_root);
  fake_unityplayer_thiz = jni_make_object("com/unity3d/player/UnityPlayer");
  fake_context_obj      = jni_make_object("android/content/Context");
  fake_surface_obj      = jni_make_object("android/view/Surface");
}
