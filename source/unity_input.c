/* MotionEvent and KeyEvent shims for nativeInjectEvent. */

#include <string.h>
#include <time.h>
#include "unity_input.h"

struct FakeID { uint32_t tag; char cls[96]; char name[64]; char sig[160]; };

enum { UI_TAG = 0x55494531 /*'UIE1'*/, KIND_MOTION, KIND_KEY };

typedef struct {
  uint32_t tag; int kind;
  int   action;
  int   count;
  int   ids[UI_MAX_POINTERS];
  float xs [UI_MAX_POINTERS];
  float ys [UI_MAX_POINTERS];
  int   keycode;
  int64_t time_ms;
} UEvent;

static UEvent g_ev;

static int64_t now_ms(void){
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
  return (int64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}
static int  has(const char *s,const char *sub){ return strstr(s,sub)!=NULL; }

void *unity_motionevent(int action,int count,const int *ids,const float *xs,const float *ys){
  UEvent *e=&g_ev; memset(e,0,sizeof *e);
  e->tag=UI_TAG; e->kind=KIND_MOTION; e->action=action; e->time_ms=now_ms();
  if (count>UI_MAX_POINTERS) count=UI_MAX_POINTERS;
  e->count=count;
  for (int i=0;i<count;i++){ e->ids[i]=ids?ids[i]:i; e->xs[i]=xs?xs[i]:0; e->ys[i]=ys?ys[i]:0; }
  return e;
}
void *unity_keyevent(int action,int keycode){
  UEvent *e=&g_ev; memset(e,0,sizeof *e);
  e->tag=UI_TAG; e->kind=KIND_KEY; e->action=action; e->keycode=keycode; e->time_ms=now_ms();
  return e;
}

/* Keep MotionEvent.obtain copies alive across frames. */
static UEvent   g_ev_copies[16];
static unsigned g_ev_copy_i;
void *unity_motionevent_obtain(void *src){
  UEvent *s = src;
  if (!s || s->tag!=UI_TAG) return src;
  UEvent *d = &g_ev_copies[g_ev_copy_i++ & 15];
  *d = *s;
  return d;
}

int input_owns_class(const char *cls){
  return has(cls,"view/MotionEvent") || has(cls,"view/KeyEvent") ||
         has(cls,"view/InputEvent");
}
/* GetObjectClass() reports java/lang/Object, so route tagged event receivers. */
int input_owns_recv(const void *recv){
  const UEvent *e = recv; return e && e->tag==UI_TAG;
}
int input_recv_is_motion(const void *recv){
  const UEvent *e = recv; return e && e->tag==UI_TAG && e->kind==KIND_MOTION;
}

static int ptr_index(const struct FakeID *id, va_list va){
  if (strstr(id->sig,"(I)")) { int idx=va_arg(va,int); return idx; }
  return 0;
}

uint64_t input_dispatch_int(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  UEvent *e = recv; const char *m=id->name;
  if (!e || e->tag!=UI_TAG) return 0;

  if (has(m,"getDeviceId")) return 0;
  if (has(m,"getSource"))   return (uint64_t)(e->kind==KIND_MOTION?AINPUT_SOURCE_TOUCHSCREEN:AINPUT_SOURCE_KEYBOARD);
  if (has(m,"getEventTime")||has(m,"getDownTime")) return (uint64_t)e->time_ms; /* long */
  if (has(m,"getMetaState")) return 0;
  if (has(m,"getFlags"))     return 0;

  if (e->kind==KIND_MOTION){
    if (has(m,"getActionMasked")) return (uint64_t)(e->action & AMOTION_ACTION_MASK);
    if (has(m,"getActionIndex"))  return (uint64_t)((e->action>>AMOTION_ACTION_PTR_IDX_SHIFT)&0xff);
    if (has(m,"getAction"))       return (uint64_t)e->action;
    if (has(m,"getPointerCount")) return (uint64_t)e->count;
    if (has(m,"getPointerId")){ int i=va_arg(va,int); return (uint64_t)((i>=0&&i<e->count)?e->ids[i]:0); }
    if (has(m,"getToolType"))     return AMOTION_TOOL_TYPE_FINGER;
    if (has(m,"getButtonState"))  return 0;
  if (has(m,"getHistorySize"))  return 0;
    return 0;
  }
  if (has(m,"getKeyCode"))     return (uint64_t)e->keycode;
  if (has(m,"getAction"))      return (uint64_t)e->action;
  if (has(m,"getRepeatCount")) return 0;
  if (has(m,"getUnicodeChar")||has(m,"GetUnicodeChar")) return 0;
  return 0;
}

float input_dispatch_float(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  UEvent *e = recv; const char *m=id->name;
  if (!e || e->tag!=UI_TAG || e->kind!=KIND_MOTION) return 0.0f;
  int i = ptr_index(id, va);
  if (i<0 || i>=e->count) i=0;
  if (has(m,"getRawX")||(has(m,"getX"))) return e->count? e->xs[i] : 0.0f;
  if (has(m,"getRawY")||(has(m,"getY"))) return e->count? e->ys[i] : 0.0f;
  if (has(m,"getPressure"))   return 1.0f;
  if (has(m,"getSize"))       return 0.1f;
  if (has(m,"getOrientation"))return 0.0f;
  return 0.0f;
}
