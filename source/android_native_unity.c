/* NDK window, looper, sensor, and input shims used by Unity. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <switch.h>
#include <GLES3/gl3.h>
#include "util.h"
#include "config.h"

typedef struct ANativeWindow ANativeWindow;
typedef struct ALooper       ALooper;

static u32 g_w = ACPC_HANDHELD_WIDTH, g_h = ACPC_HANDHELD_HEIGHT;

void android_native_update_mode(void){
  if (appletGetOperationMode() == AppletOperationMode_Console) {
    g_w = screen_width = ACPC_DOCKED_WIDTH;
    g_h = screen_height = ACPC_DOCKED_HEIGHT;
  } else {
    g_w = screen_width = ACPC_HANDHELD_WIDTH;
    g_h = screen_height = ACPC_HANDHELD_HEIGHT;
  }
}

/* Crop aligned buffers and rotate the portrait surface for display. */
static u32 nx_portrait_transform(void) {
  return config.portrait == 2 ? (u32)NATIVE_WINDOW_TRANSFORM_ROT_270
                              : (u32)NATIVE_WINDOW_TRANSFORM_ROT_90;
}
static void nx_window_set_geom(NWindow *w, u32 bw, u32 bh) {
  nwindowSetDimensions(w, bw, bh);
  nwindowSetCrop(w, 0, 0, bw, bh);
  nwindowSetTransform(w, nx_portrait_transform());
}

static ANativeWindow *android_native_window(void){
  NWindow *w = nwindowGetDefault();
  nx_window_set_geom(w, g_w, g_h);
  return (ANativeWindow *)w;
}
void     ANativeWindow_acquire(ANativeWindow *w){ (void)w; }
void     ANativeWindow_release(ANativeWindow *w){ (void)w; }
ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface){
  (void)env; (void)surface; return android_native_window();
}
int32_t  ANativeWindow_getWidth (ANativeWindow *w){ (void)w; return (int32_t)g_w; }
int32_t  ANativeWindow_getHeight(ANativeWindow *w){ (void)w; return (int32_t)g_h; }
int32_t  ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format){
  (void)width;
  (void)height;
  (void)format;
  nx_window_set_geom((NWindow *)w, g_w, g_h);
  return 0;
}

/* Unity uses ALooper as a per-thread wait/wake primitive. */
#define ALOOPER_POLL_WAKE     (-1)
#define ALOOPER_POLL_TIMEOUT  (-3)
#define MAX_LOOPERS 16

struct ALooper { Mutex m; CondVar cv; int signaled; int refs; u32 owner; int used; };
static struct ALooper g_loopers[MAX_LOOPERS];
static Mutex g_loopers_lock;
static int   g_loopers_init = 0;

static void loopers_once(void){ if(!g_loopers_init){ mutexInit(&g_loopers_lock); g_loopers_init=1; } }

static struct ALooper *looper_for(u32 tid, int create){
  loopers_once();
  mutexLock(&g_loopers_lock);
  for (int i=0;i<MAX_LOOPERS;i++) if (g_loopers[i].used && g_loopers[i].owner==tid){
    struct ALooper *l=&g_loopers[i]; mutexUnlock(&g_loopers_lock); return l; }
  if (create) for (int i=0;i<MAX_LOOPERS;i++) if (!g_loopers[i].used){
    struct ALooper *l=&g_loopers[i];
    l->used=1; l->owner=tid; l->signaled=0; l->refs=1;
    mutexInit(&l->m); condvarInit(&l->cv);
    mutexUnlock(&g_loopers_lock); return l; }
  mutexUnlock(&g_loopers_lock);
  return NULL;
}
static u32 cur_tid(void){ return (u32)(uintptr_t)threadGetCurHandle(); }

ALooper *ALooper_prepare(int opts){ (void)opts; return (ALooper *)looper_for(cur_tid(), 1); }
ALooper *ALooper_forThread(void){  return (ALooper *)looper_for(cur_tid(), 0); }
void     ALooper_acquire(ALooper *l){ struct ALooper *L=(void*)l; if(L){ mutexLock(&L->m); L->refs++; mutexUnlock(&L->m);} }
void     ALooper_release(ALooper *l){ struct ALooper *L=(void*)l; if(L){ mutexLock(&L->m); if(--L->refs<=0) L->used=0; mutexUnlock(&L->m);} }

void ALooper_wake(ALooper *l){
  struct ALooper *L=(void*)l; if(!L) return;
  mutexLock(&L->m); L->signaled=1; condvarWakeAll(&L->cv); mutexUnlock(&L->m);
}
int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData){
  struct ALooper *L = (void*)looper_for(cur_tid(), 1);
  if (outFd) *outFd=0;
  if (outEvents) *outEvents=0;
  if (outData) *outData=NULL;
  mutexLock(&L->m);
  if (!L->signaled){
    if (timeoutMillis==0){ mutexUnlock(&L->m); return ALOOPER_POLL_TIMEOUT; }
    if (timeoutMillis<0)  condvarWait(&L->cv,&L->m);
    else condvarWaitTimeout(&L->cv,&L->m,(u64)timeoutMillis*1000000ull);
  }
  int was = L->signaled; L->signaled=0;
  mutexUnlock(&L->m);
  return was ? ALOOPER_POLL_WAKE : ALOOPER_POLL_TIMEOUT;
}
int ALooper_addFd(ALooper *l,int fd,int ident,int events,void *cb,void *data){
  (void)l;(void)fd;(void)ident;(void)events;(void)cb;(void)data; return 1; }

void *ASensorManager_getInstance(void){ static int x; return &x; }
int   ASensorManager_getSensorList(void *m, void **list){ (void)m; if(list)*list=NULL; return 0; }
void *ASensorManager_getDefaultSensor(void *m, int type){ (void)m;(void)type; return NULL; }
void *ASensorManager_createEventQueue(void *m, void *looper, int ident, void *cb, void *data){
  (void)m;(void)looper;(void)ident;(void)cb;(void)data; static int q; return &q; }
int   ASensorManager_destroyEventQueue(void *m, void *q){ (void)m;(void)q; return 0; }

int   ASensorEventQueue_enableSensor (void *q, const void *s){ (void)q;(void)s; return -1; }
int   ASensorEventQueue_disableSensor(void *q, const void *s){ (void)q;(void)s; return 0; }
int   ASensorEventQueue_setEventRate (void *q, const void *s, int32_t us){ (void)q;(void)s;(void)us; return 0; }
int   ASensorEventQueue_getEvents    (void *q, void *ev, size_t n){ (void)q;(void)ev;(void)n; return 0; }
int   ASensorEventQueue_hasEvents    (void *q){ (void)q; return 0; }

const char *ASensor_getName      (const void *s){ (void)s; return ""; }
const char *ASensor_getVendor    (const void *s){ (void)s; return ""; }
int         ASensor_getType      (const void *s){ (void)s; return 0; }
float       ASensor_getResolution(const void *s){ (void)s; return 0.0f; }
int         ASensor_getMinDelay  (const void *s){ (void)s; return 0; }

void android_get_orientation(float *x, float *y, float *z){
  if (x) *x = 0.0f;
  if (y) *y = 0.0f;
  if (z) *z = 0.0f;
}

/* Handheld touch and docked virtual cursor input. */
#include "unity_input.h"

static PadState g_pad;
static HidTouchScreenState g_touch;
static int   g_prev_touch = 0;
static float g_cursor_x = 640, g_cursor_y = 360;
static float g_last_tx = 360, g_last_ty = 640;
static int   g_prev_a = 0;
static int   g_cursor_shown = 0;

void android_native_input_init(void){
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  Result rc = hidSetNpadJoyHoldType(HidNpadJoyHoldType_Vertical);
  if (R_FAILED(rc))
    printf_fake("[hid] WARNING: failed to set vertical Joy-Con hold type: 0x%x\n", rc);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();
  g_cursor_x = g_last_tx = (float)g_w * 0.5f;
  g_cursor_y = g_last_ty = (float)g_h * 0.5f;
}

typedef uint8_t (*inject_fn)(void*,void*,void*,int);

void android_native_feed_hid(inject_fn inject, void *env, void *thiz){
  padUpdate(&g_pad);

  int n = hidGetTouchScreenStates(&g_touch, 1);
  if (n > 0 && g_touch.count > 0){
    g_cursor_shown = 0;
    int   ids[UI_MAX_POINTERS]; float xs[UI_MAX_POINTERS]; float ys[UI_MAX_POINTERS];
    int c = g_touch.count > UI_MAX_POINTERS ? UI_MAX_POINTERS : g_touch.count;
    /* Map the 1280x720 touch panel into the rotated portrait surface. */
    const float PANEL_W = 1280.0f, PANEL_H = 720.0f;
    for (int i=0;i<c;i++){ ids[i]=(int)g_touch.touches[i].finger_id;
      float px=(float)g_touch.touches[i].x, py=(float)g_touch.touches[i].y;
      if (config.portrait == 2) {
        xs[i]= (PANEL_H-py) * ((float)g_w / PANEL_H);
        ys[i]=  px          * ((float)g_h / PANEL_W);
      } else {
        xs[i]=  py          * ((float)g_w / PANEL_H);
        ys[i]= (PANEL_W-px) * ((float)g_h / PANEL_W);
      }
    }
    g_last_tx = xs[0]; g_last_ty = ys[0];
    int action = g_prev_touch ? AMOTION_ACTION_MOVE : AMOTION_ACTION_DOWN;
    inject(env, thiz, unity_motionevent(action, c, ids, xs, ys), 0);
    g_prev_touch = c;
    return;
  }
  if (g_prev_touch){
    int   ids[1]={0}; float xs[1]={g_last_tx}, ys[1]={g_last_ty};
    inject(env, thiz, unity_motionevent(AMOTION_ACTION_UP, 1, ids, xs, ys), 0);
    g_prev_touch = 0;
    return;
  }

  /* A single right Joy-Con reports its stick as the right stick; all full
   * controllers and left/dual Joy-Cons use the left stick. */
  u32 style = padGetStyleSet(&g_pad);
  int right_joy_only = (style & HidNpadStyleTag_NpadJoyRight) &&
                       !(style & (HidNpadStyleTag_NpadFullKey |
                                  HidNpadStyleTag_NpadHandheld |
                                  HidNpadStyleTag_NpadJoyDual |
                                  HidNpadStyleTag_NpadJoyLeft));
  HidAnalogStickState ls = padGetStickPos(&g_pad, right_joy_only ? 1 : 0);
  float sx = (ls.x / 32767.0f) * 14.0f, sy = (ls.y / 32767.0f) * 14.0f;
  if (padIsHandheld(&g_pad)) {
    /* Attached Joy-Cons rotate with the display, so convert their axes through
     * the same portrait transform used by the compositor. */
    if (config.portrait == 2) { g_cursor_x += sy; g_cursor_y += sx; }
    else                      { g_cursor_x -= sy; g_cursor_y -= sx; }
  } else {
    /* Detached vertical Joy-Cons and external controllers already use portrait
     * axes. Rotating those inputs a second time is what inverted the cursor. */
    g_cursor_x += sx;
    g_cursor_y -= sy;
  }
  if (g_cursor_x < 0) g_cursor_x = 0;
  if (g_cursor_x > g_w) g_cursor_x = g_w;
  if (g_cursor_y < 0) g_cursor_y = 0;
  if (g_cursor_y > g_h) g_cursor_y = g_h;
  if (sx*sx + sy*sy > 0.25f) g_cursor_shown = 1;

  int a = (padGetButtons(&g_pad) & HidNpadButton_A) ? 1 : 0;
  if (a) g_cursor_shown = 1;
  int ids[1]={0}; float xs[1]={g_cursor_x}, ys[1]={g_cursor_y};
  if (a && !g_prev_a)      inject(env, thiz, unity_motionevent(AMOTION_ACTION_DOWN, 1, ids, xs, ys), 0);
  else if (a && g_prev_a)  inject(env, thiz, unity_motionevent(AMOTION_ACTION_MOVE, 1, ids, xs, ys), 0);
  else if (!a && g_prev_a) inject(env, thiz, unity_motionevent(AMOTION_ACTION_UP,   1, ids, xs, ys), 0);
  g_prev_a = a;

  /* B maps to Android Back. */
  static int prev_b = 0;
  int b = (padGetButtons(&g_pad) & HidNpadButton_B) ? 1 : 0;
  if (b && !prev_b) inject(env, thiz, unity_keyevent(AKEY_ACTION_DOWN, AKEYCODE_BACK), 0);
  if (!b && prev_b) inject(env, thiz, unity_keyevent(AKEY_ACTION_UP,   AKEYCODE_BACK), 0);
  prev_b = b;
}

/* Draw the docked cursor before the rotated surface is presented. */
static GLuint cur_link(const char *vs, const char *fs){
  GLuint v=glCreateShader(GL_VERTEX_SHADER);   glShaderSource(v,1,&vs,0); glCompileShader(v);
  GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
  GLuint p=glCreateProgram(); glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
  GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  glDeleteShader(v); glDeleteShader(f);
  if(!ok){ glDeleteProgram(p); return 0; }
  return p;
}
void android_native_draw_cursor(void){
  if (!g_cursor_shown) return;
  static struct { GLuint prog; GLint pos, loc, feather; int tried; } c = {0,0,0,0,0};
  if (!c.tried){
    c.tried = 1;
    c.prog = cur_link(
      "attribute vec2 aPos; attribute vec2 aLocal; varying vec2 vL;"
      "void main(){ vL=aLocal; gl_Position=vec4(aPos,0.0,1.0); }",
      "precision mediump float; varying vec2 vL; uniform float uF;"
      "void main(){ float d=length(vL);"
      " float a=1.0-smoothstep(1.0-uF,1.0,d);"
      " float core=1.0-smoothstep(0.72-uF,0.72+uF,d);"
      " gl_FragColor=vec4(mix(vec3(0.05),vec3(0.98),core), a*0.85); }");
    if (c.prog){ c.pos=glGetAttribLocation(c.prog,"aPos"); c.loc=glGetAttribLocation(c.prog,"aLocal"); c.feather=glGetUniformLocation(c.prog,"uF"); }
  }
  if (!c.prog) return;

  float cx = (g_cursor_x / (float)g_w) * 2.0f - 1.0f;
  float cy = 1.0f - (g_cursor_y / (float)g_h) * 2.0f;
  float r  = 18.0f * ((float)(g_w > g_h ? g_w : g_h) / 1280.0f);
  float rx = r/(float)g_w*2.0f, ry = r/(float)g_h*2.0f;
  const GLfloat pos[8]  = { cx-rx,cy-ry, cx+rx,cy-ry, cx-rx,cy+ry, cx+rx,cy+ry };
  static const GLfloat local[8] = { -1,-1, 1,-1, -1,1, 1,1 };

  GLint pprog,pbuf,pvao,pvp[4],bsr,bdr,bsa,bda,ber,bea;
  GLboolean e_bl=glIsEnabled(GL_BLEND), e_dp=glIsEnabled(GL_DEPTH_TEST),
            e_sc=glIsEnabled(GL_SCISSOR_TEST), e_cu=glIsEnabled(GL_CULL_FACE);
  glGetIntegerv(GL_CURRENT_PROGRAM,&pprog); glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&pbuf);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&pvao); glGetIntegerv(GL_VIEWPORT,pvp);
  glGetIntegerv(GL_BLEND_SRC_RGB,&bsr); glGetIntegerv(GL_BLEND_DST_RGB,&bdr);
  glGetIntegerv(GL_BLEND_SRC_ALPHA,&bsa); glGetIntegerv(GL_BLEND_DST_ALPHA,&bda);
  glGetIntegerv(GL_BLEND_EQUATION_RGB,&ber); glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&bea);

  glBindVertexArray(0);                                /* scratch VAO: client arrays allowed */
  glViewport(0,0,(GLsizei)g_w,(GLsizei)g_h);
  glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glBindBuffer(GL_ARRAY_BUFFER,0); glUseProgram(c.prog); glUniform1f(c.feather, 2.5f/r);
  glEnableVertexAttribArray(c.pos); glEnableVertexAttribArray(c.loc);
  glVertexAttribPointer(c.pos,2,GL_FLOAT,GL_FALSE,0,pos);
  glVertexAttribPointer(c.loc,2,GL_FLOAT,GL_FALSE,0,local);
  glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  glDisableVertexAttribArray(c.pos); glDisableVertexAttribArray(c.loc);

  glBindBuffer(GL_ARRAY_BUFFER,(GLuint)pbuf); glBindVertexArray((GLuint)pvao);
  glUseProgram((GLuint)pprog); glViewport(pvp[0],pvp[1],pvp[2],pvp[3]);
  glBlendEquationSeparate((GLenum)ber,(GLenum)bea);
  glBlendFuncSeparate((GLenum)bsr,(GLenum)bdr,(GLenum)bsa,(GLenum)bda);
  if(!e_bl) glDisable(GL_BLEND);
  if(e_dp)  glEnable(GL_DEPTH_TEST);
  if(e_sc)  glEnable(GL_SCISSOR_TEST);
  if(e_cu)  glEnable(GL_CULL_FACE);
}
