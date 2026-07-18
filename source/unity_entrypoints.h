/* Unity 2022.3.28f1 arm64 link-time entry points. */
#ifndef UNITY_ENTRYPOINTS_H
#define UNITY_ENTRYPOINTS_H

#include <stdint.h>
#include "so_util.h"

/* UnityPlayer lifecycle. */
#define OFF_JNI_OnLoad                    0x9e138c /* (JavaVM*,reserved)->jint  caches VM, registers natives */
#define OFF_initJni                       0x9e053c /* (env,thiz,Context)                 */
#define OFF_nativeRecreateGfxState        0x9e0770 /* (env,thiz,int,Surface)  set surface*/
#define OFF_nativeSendSurfaceChangedEvent 0x9e07d8 /* (env,thiz)                         */
#define OFF_nativeRender                  0x9e0830 /* (env,thiz)->Z   per-frame; false=stop */
#define OFF_nativeInjectEvent             0x9e0890 /* (env,thiz,InputEvent)->Z            */
#define OFF_nativeResume                  0x9e063c /* (env,thiz)->V                      */
#define OFF_nativeFocusChanged            0x9e071c /* (env,thiz,Z)                       */
#define OFF_nativeDone                    0x9e0548 /* (env,thiz)->Z   shutdown           */
#define OFF_nativeApplicationUnload       0x9e06cc /* (env,thiz)                         */
/* Software keyboard. */
#define OFF_nativeSetKeyboardIsVisible    0x9e0c08
#define OFF_nativeSetInputString          0x9e0c60
#define OFF_nativeSoftInputClosed         0x9e0e50
#define OFF_nativeSoftInputCanceled       0x9e0d68

/* TimeManager entry and frameless body used by the native frame clock. */
#define OFF_TimeManager_Update_entry      0x80b204 /* prologue: [+0xc8]++;[+0xd0]++;if([+0xf8])ret */
#define OFF_TimeManager_Update_body       0x80b228 /* frameless; re-reads x0, expects newTime in d0 */

/* JNI lifecycle signatures. */
typedef void     (*fn_initJni)(void*,void*,void*);
typedef void     (*fn_gfxstate)(void*,void*,int32_t,void*);
typedef void     (*fn_v)(void*,void*);
typedef uint8_t  (*fn_z)(void*,void*);
typedef void     (*fn_vz)(void*,void*,int32_t);
typedef uint8_t  (*fn_inject)(void*,void*,void*,int32_t);

#define UNITY_RESOLVE(mod, off) ((void*)((uintptr_t)(mod).load_virtbase + (off)))

#endif
