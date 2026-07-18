/* jni_fake.c -- fake JNI environment for Pocket Camp's Android Unity player
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <switch.h>

#include "config.h"
#include "util.h"
#include "jni_fake.h"
#include "data.h"
#include "editbox.h"
#include "android_native_unity.h"
#include "jni_unimpl.h"
#include "libc_shim.h"   /* managed_path: device-less paths for managed code */

#define JNI_OK 0
#define JNI_VERSION_1_6 0x00010006

typedef uint64_t juint;

enum {
  TAG_OBJECT = 0x4f424a31, // 'OBJ1'  heap object (freeable)
  TAG_STRING = 0x53545231, // 'STR1'
  TAG_OBJARR = 0x4f415231, // 'OAR1'
  TAG_PRIARR = 0x50415231, // 'PAR1'
  TAG_ID     = 0x4d494431, // 'MID1'  pooled, never freed
  TAG_CLASS  = 0x434c5331, // 'CLS1'  pooled, never freed
};

typedef struct { uint32_t tag; char label[64]; } FakeObject;
typedef struct { uint32_t tag; char *utf; } FakeString;
typedef struct { uint32_t tag; int len; void **items; } FakeObjArray;
typedef struct { uint32_t tag; int len; int elem_size; void *data; } FakePriArray;
typedef struct { uint32_t tag; char cls[96]; char name[64]; char sig[160]; } FakeID;
typedef struct { uint32_t tag; char name[96]; } FakeClass;

/* Local references follow PushLocalFrame/PopLocalFrame. */
#define MAX_LOCALS 1048576
#define MAX_FRAMES 64
static void *locals[MAX_LOCALS];
static int locals_top = 0;
static int frames[MAX_FRAMES];
static int frame_top = 0;
static Mutex locals_lock;

static void *reg_local(void *ref) {
  if (ref) {
    mutexLock(&locals_lock);
    if (locals_top < MAX_LOCALS)
      locals[locals_top++] = ref;
    mutexUnlock(&locals_lock);
  }
  return ref;
}

/* Intern repeated JNI strings outside the local-reference table. */
#define MAX_ISTR 2048
static FakeString istr_pool[MAX_ISTR];
static int istr_count = 0;

static void free_ref(void *ref) {
  if (!ref)
    return;
  if ((char *)ref >= (char *)istr_pool && (char *)ref < (char *)&istr_pool[MAX_ISTR])
    return;  // interned string -- pooled, never freed
  switch (*(uint32_t *)ref) {
    case TAG_STRING: { FakeString *s = ref; free(s->utf); free(s); break; }
    case TAG_PRIARR: { FakePriArray *a = ref; free(a->data); free(a); break; }
    case TAG_OBJARR: { FakeObjArray *a = ref; free(a->items); free(a); break; }
    case TAG_OBJECT: free(ref); break;
    default: break; // TAG_ID / TAG_CLASS are pooled
  }
}

static void delete_local(void *ref) {
  if (!ref)
    return;
  mutexLock(&locals_lock);
  for (int i = locals_top - 1; i >= 0; i--) {
    if (locals[i] == ref) {
      locals[i] = locals[--locals_top];
      free_ref(ref);
      break;
    }
  }
  mutexUnlock(&locals_lock);
}

/* One pooled opaque object per class label. */
#define MAX_IOBJ 512
static FakeObject iobj_pool[MAX_IOBJ];
static int iobj_count = 0;
void *jni_make_object(const char *label) {
  const char *l = (label && label[0]) ? label : "obj";
  mutexLock(&locals_lock);
  void *r = NULL;
  for (int i = 0; i < iobj_count; i++)
    if (!strcmp(iobj_pool[i].label, l)) { r = &iobj_pool[i]; break; }
  if (!r) {
    if (iobj_count >= MAX_IOBJ) r = &iobj_pool[0];
    else {
      FakeObject *o = &iobj_pool[iobj_count++];
      o->tag = TAG_CLASS;             // pooled: free_ref() ignores TAG_CLASS
      strncpy(o->label, l, sizeof(o->label) - 1);
      r = o;
    }
  }
  mutexUnlock(&locals_lock);
  return r;
}

void *jni_make_string(const char *utf) {
  const char *u = utf ? utf : "";
  mutexLock(&locals_lock);
  for (int i = 0; i < istr_count; i++)            // repeats reuse the pooled string
    if (!strcmp(istr_pool[i].utf, u)) { void *r = &istr_pool[i]; mutexUnlock(&locals_lock); return r; }
  if (istr_count < MAX_ISTR) {
    FakeString *s = &istr_pool[istr_count++];
    s->tag = TAG_STRING;
    s->utf = strdup(u);
    mutexUnlock(&locals_lock);
    return s;                                      // pooled, not reg_local'd
  }
  mutexUnlock(&locals_lock);
  FakeString *s = calloc(1, sizeof(*s));           // pool full: one-off local string
  s->tag = TAG_STRING;
  s->utf = strdup(u);
  return reg_local(s);
}

static void *make_pri_array_adopt(void *data, int len, int elem_size) {
  FakePriArray *a = calloc(1, sizeof(*a));
  a->tag = TAG_PRIARR;
  a->len = len;
  a->elem_size = elem_size;
  a->data = data;
  return reg_local(a);
}

static const char *obj_str(void *jstr) {
  FakeString *s = jstr;
  if (s && s->tag == TAG_STRING)
    return s->utf;
  return "";
}

/* Java UTF-16 code-unit count for a UTF-8 string. */
static juint utf16_len(const char *str) {
  const unsigned char *p = (const unsigned char *)(str ? str : "");
  juint n = 0;
  while (*p) {
    const unsigned char c = *p;
    juint adv; uint32_t cp;
    if (c < 0x80)      { cp = c;        adv = 1; }
    else if (c < 0xE0) { cp = c & 0x1F; adv = 2; }
    else if (c < 0xF0) { cp = c & 0x0F; adv = 3; }
    else               { cp = c & 0x07; adv = 4; }
    for (juint k = 1; k < adv; k++) {
      if (!p[k]) { adv = k; break; }
      cp = (cp << 6) | (p[k] & 0x3F);
    }
    n += (cp >= 0x10000) ? 2u : 1u;
    p += adv;
  }
  return n;
}

#define MAX_CLASSES 512
static FakeClass class_pool[MAX_CLASSES];
static int class_count = 0;

static void *intern_class(const char *name) {
  for (int i = 0; i < class_count; i++)
    if (!strcmp(class_pool[i].name, name))
      return &class_pool[i];
  if (class_count >= MAX_CLASSES) {
    return &class_pool[0];
  }
  FakeClass *c = &class_pool[class_count++];
  c->tag = TAG_CLASS;
  strncpy(c->name, name, sizeof(c->name) - 1);
  return c;
}

static const char *class_name_of(void *cls) {
  FakeClass *c = cls;
  return (c && c->tag == TAG_CLASS) ? c->name : "";
}

static FakeObject *g_asset_mgr = NULL;

static void *get_asset_manager_obj(void) {
  if (!g_asset_mgr) {
    g_asset_mgr = calloc(1, sizeof(*g_asset_mgr));
    g_asset_mgr->tag = TAG_CLASS;
    strcpy(g_asset_mgr->label, "AssetManager");
  }
  return g_asset_mgr;
}

static FakeObject *g_classloader = NULL;
static void *get_classloader_obj(void) {
  if (!g_classloader) {
    g_classloader = calloc(1, sizeof(*g_classloader));
    g_classloader->tag = TAG_CLASS;
    strcpy(g_classloader->label, "ClassLoader");
  }
  return g_classloader;
}

#define MAX_IDS 4096
static FakeID id_pool[MAX_IDS];
static int id_count = 0;

static FakeID *get_id(const char *cls, const char *name, const char *sig) {
  for (int i = 0; i < id_count; i++)
    if (!strcmp(id_pool[i].name, name) && !strcmp(id_pool[i].sig, sig) &&
        !strcmp(id_pool[i].cls, cls))
      return &id_pool[i];
  if (id_count >= MAX_IDS) {
    return &id_pool[0];
  }
  FakeID *id = &id_pool[id_count++];
  id->tag = TAG_ID;
  strncpy(id->cls, cls ? cls : "", sizeof(id->cls) - 1);
  strncpy(id->name, name, sizeof(id->name) - 1);
  strncpy(id->sig, sig, sizeof(id->sig) - 1);
  return id;
}

static int sig_returns(const char *sig, const char *ret) {
  const char *rp = strchr(sig, ')');
  return rp && strstr(rp + 1, ret) == rp + 1;
}

static int name_has(const char *name, const char *sub) { return strstr(name, sub) != NULL; }

static const char *lang_code(void) {
  if (config.language == LANG_JA) return "ja";
  if (config.language == LANG_EN) return "en";
  /* Resolve the system language once. */
  static const char *cached = NULL;
  if (!cached) {
    cached = "en";
    u64 code; SetLanguage sl;
    if (R_SUCCEEDED(setInitialize())) {
      if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &sl))) {
        switch (sl) {
          case SetLanguage_JA:                            cached = "ja"; break;
          case SetLanguage_FR: case SetLanguage_FRCA:     cached = "fr"; break;
          case SetLanguage_DE:                            cached = "de"; break;
          case SetLanguage_IT:                            cached = "it"; break;
          case SetLanguage_ES: case SetLanguage_ES419:    cached = "es"; break;
          case SetLanguage_PT: case SetLanguage_PTBR:     cached = "pt"; break;
          case SetLanguage_NL:                            cached = "nl"; break;
          case SetLanguage_RU:                            cached = "ru"; break;
          case SetLanguage_KO:                            cached = "ko"; break;
          case SetLanguage_ZHCN: case SetLanguage_ZHHANS:
          case SetLanguage_ZHTW: case SetLanguage_ZHHANT: cached = "zh"; break;
          default:                                        cached = "en"; break;
        }
      }
      setExit();
    }
  }
  return cached;
}

/* Locale values matching lang_code(). */
typedef struct { const char *lang, *ctry, *iso3l, *iso3c, *loc; } LangRow;
static const LangRow *lang_row(void) {
  static const LangRow rows[] = {
    {"en","US","eng","USA","en_US"}, {"ja","JP","jpn","JPN","ja_JP"},
    {"fr","FR","fra","FRA","fr_FR"}, {"de","DE","deu","DEU","de_DE"},
    {"it","IT","ita","ITA","it_IT"}, {"es","ES","spa","ESP","es_ES"},
    {"pt","PT","por","PRT","pt_PT"}, {"nl","NL","nld","NLD","nl_NL"},
    {"ru","RU","rus","RUS","ru_RU"}, {"ko","KR","kor","KOR","ko_KR"},
    {"zh","CN","zho","CHN","zh_CN"},
  };
  const char *l = lang_code();
  for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++)
    if (!strcmp(rows[i].lang, l)) return &rows[i];
  return &rows[0];
}

const char *jni_locale_language(void) { return lang_row()->lang; }
const char *jni_locale_country(void)  { return lang_row()->ctry; }
const char *jni_locale_name(void)     { return lang_row()->loc; }

/* Locale methods may be resolved through java/lang/Object. */
static void *locale_object_result(const char *name) {
  const LangRow *lr = lang_row();
  if (!strcmp(name, "getDefault")) {
    return jni_make_object("java/util/Locale");
  }
  if (!strcmp(name, "getLanguage"))    return jni_make_string(lr->lang);
  if (!strcmp(name, "getCountry"))     return jni_make_string(lr->ctry);
  if (!strcmp(name, "getISO3Language"))return jni_make_string(lr->iso3l);
  if (!strcmp(name, "getISO3Country")) return jni_make_string(lr->iso3c);
  if (!strcmp(name, "toString") || name_has(name, "getDisplayName")) {
    return jni_make_string(lr->loc);
  }
  return NULL;
}

static int recv_has_label(const void *recv, const char *needle) {
  if (!recv) return 0;
  const uint32_t tag = *(const uint32_t *)recv;
  if (tag != TAG_OBJECT && tag != TAG_CLASS) return 0;
  return name_has(((const FakeObject *)recv)->label, needle);
}

/* Return the first non-empty String argument. */
static const char *first_string_arg(const char *sig, va_list va) {
  const char *p = sig ? strchr(sig, '(') : NULL;
  if (!p) return "";
  for (p++; *p && *p != ')'; p++) {
    switch (*p) {
      case 'I': case 'Z': case 'B': case 'C': case 'S': (void)va_arg(va, int); break;
      case 'F': case 'D': (void)va_arg(va, double); break;
      case 'J': (void)va_arg(va, long long); break;
      case '[':
        (void)va_arg(va, void *);
        if (p[1] == 'L') { p++; while (*p && *p != ';') p++; } else if (p[1]) p++;
        break;
      case 'L': {
        const char *s = obj_str(va_arg(va, void *));
        while (*p && *p != ';') p++;
        if (s && s[0]) return s;
        break;
      }
      default: break;
    }
  }
  return "";
}

/* Software-keyboard method names. */
static int is_editbox_show(const char *n)  { return name_has(n, "ShowEditBox")  || name_has(n, "OpenEditBox")  || name_has(n, "ShowTextBox") || name_has(n, "OpenTextBox"); }
static int is_editbox_open(const char *n)  { return name_has(n, "IsOpenEditBox") || name_has(n, "IsOpenTextBox"); }
static int is_editbox_text(const char *n)  { return name_has(n, "GetEditBoxText") || name_has(n, "GetTextBoxText"); }
static int is_editbox_close(const char *n) { return name_has(n, "CloseEditBox") || name_has(n, "CloseTextBox"); }

const char *jni_string_utf(void *jstr);

/* AudioManager properties used by FMOD. */
static int g_last_output_prop = 0;

static void *getproperty_value(const char *key) {
  int which = 0;
  if (key && strstr(key, "SAMPLE_RATE"))            which = 1;
  else if (key && strstr(key, "FRAMES_PER_BUFFER")) which = 2;
  else                                              which = g_last_output_prop;
  if (which == 1) return jni_make_string("48000");   /* native output sample rate */
  if (which == 2) return jni_make_string("256");     /* native frames-per-buffer */
  return jni_make_string("");
}

static void *native_gallery_save(void *path_obj) {
  const char *path = obj_str(path_obj);
  if (!path || !path[0]) return jni_make_string("");

  char source[768], destination[768];
  if (path[0] == '/') snprintf(source, sizeof source, "sdmc:%s", path);
  else snprintf(source, sizeof source, "%s", path);

  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  if (!name[0] || strchr(name, '\\')) return jni_make_string("");

  mkdir(GAME_HOME "/Campicards", 0777);
  if (snprintf(destination, sizeof destination, GAME_HOME "/Campicards/%s", name) >=
      (int)sizeof destination)
    return jni_make_string("");

  FILE *in = fopen(source, "rb");
  FILE *out = in ? fopen(destination, "wb") : NULL;
  uint8_t *buffer = out ? malloc(64 * 1024) : NULL;
  int ok = buffer != NULL;
  while (ok) {
    size_t count = fread(buffer, 1, 64 * 1024, in);
    if (count && fwrite(buffer, 1, count, out) != count) ok = 0;
    if (count < 64 * 1024) {
      if (ferror(in)) ok = 0;
      break;
    }
  }
  free(buffer);
  if (out && fclose(out) != 0) ok = 0;
  if (in) fclose(in);
  if (!ok) {
    if (out) remove(destination);
    return jni_make_string("");
  }

  return jni_make_string(managed_path(destination));
}

static int is_reflection_helper(const FakeID *id) {
  return name_has(id->cls, "unity3d/player/ReflectionHelper");
}

static void *reflection_member(void *target_class, void *name_obj, void *sig_obj,
                               const char *fallback_name) {
  const char *cls = class_name_of(target_class);
  const char *name = name_obj ? obj_str(name_obj) : fallback_name;
  const char *sig = obj_str(sig_obj);
  return get_id(cls, name, sig);
}

static void *reflection_object(const FakeID *id, va_list va) {
  void *target_class = va_arg(va, void *);
  if (!strcmp(id->name, "getConstructorID"))
    return reflection_member(target_class, NULL, va_arg(va, void *), "<init>");
  if (!strcmp(id->name, "getMethodID")) {
    void *name = va_arg(va, void *);
    void *sig = va_arg(va, void *);
    return reflection_member(target_class, name, sig, "");
  }
  if (!strcmp(id->name, "getFieldID")) {
    void *name = va_arg(va, void *);
    void *sig = va_arg(va, void *);
    return reflection_member(target_class, name, sig, "");
  }
  if (!strcmp(id->name, "getFieldSignature")) {
    const FakeID *field = target_class;
    return jni_make_string(field && field->tag == TAG_ID ? field->sig : "");
  }
  return NULL;
}

static void *reflection_object_a(const FakeID *id, const void *args) {
  void *const *a = args;
  if (!strcmp(id->name, "getConstructorID"))
    return reflection_member(a[0], NULL, a[1], "<init>");
  if (!strcmp(id->name, "getMethodID") || !strcmp(id->name, "getFieldID"))
    return reflection_member(a[0], a[1], a[2], "");
  if (!strcmp(id->name, "getFieldSignature")) {
    const FakeID *field = a[0];
    return jni_make_string(field && field->tag == TAG_ID ? field->sig : "");
  }
  return NULL;
}

static void *act_object(const FakeID *id, va_list va) {
  if (is_reflection_helper(id)) {
    void *result = reflection_object(id, va);
    if (result) return result;
  }
  if (name_has(id->cls, "NativeGallery") && !strcmp(id->name, "SaveMedia")) {
    (void)va_arg(va, void *);
    (void)va_arg(va, int);
    return native_gallery_save(va_arg(va, void *));
  }
  /* Android Choreographer is unavailable. */
  if ((name_has(id->cls, "Choreographer") || name_has(id->cls, "SwappyDisplayManager")) &&
      (name_has(id->name, "getInstance") || sig_returns(id->sig, "Landroid/view/Choreographer;")))
    return NULL;
  /* Preserve PlayerPrefs keys through Uri.encode/decode. */
  if (name_has(id->cls, "net/Uri") && (name_has(id->name, "encode") || name_has(id->name, "decode")))
    return va_arg(va, void *);
  if (name_has(id->name, "AssetManager") || sig_returns(id->sig, "Landroid/content/res/AssetManager;"))
    return get_asset_manager_obj();
  if (name_has(id->name, "ClassLoader") || sig_returns(id->sig, "Ljava/lang/ClassLoader;"))
    return get_classloader_obj();
  if (sig_returns(id->sig, "Ljava/lang/Class;"))
    return intern_class("java/lang/Object");
  if (name_has(id->name, "VersionName")) return jni_make_string(ACPC_VERSION_NAME);
  if (name_has(id->name, "PackageName")) return jni_make_string(ACPC_PACKAGE);
  if (name_has(id->name, "DeviceModel")) return jni_make_string("Switch");
  if (name_has(id->name, "getProperty"))
    return getproperty_value(jni_string_utf(va_arg(va, void *)));
  /* Stable identifier for local encrypted data. */
  if (name_has(id->cls, "provider/Settings$Secure") &&
      !strcmp(id->name, "getString")) {
    (void)va_arg(va, void *); /* ContentResolver */
    (void)va_arg(va, void *); /* ANDROID_ID key */
    return jni_make_string("414350434e585357");
  }
  if (name_has(id->name, "Language") || name_has(id->name, "language"))
    return jni_make_string(lang_code());
  /* Match Environment.MEDIA_MOUNTED. */
  if (name_has(id->cls, "os/Environment")) {
    if (name_has(id->name, "ExternalStorageState")) return jni_make_string("mounted");
    if (name_has(id->name, "Directory")) return jni_make_object("java/io/File"); /* ->getAbsolutePath */
  }
  /* Unity may resolve Locale.getDefault through java/lang/Object. */
  if (name_has(id->cls, "Locale") ||
      (!strcmp(id->name, "getDefault") && sig_returns(id->sig, "Ljava/lang/Object;"))) {
    void *r = locale_object_result(id->name);
    if (r) return r;
  }
  if (name_has(id->name, "DataPath") || name_has(id->name, "StoragePath") ||
      name_has(id->name, "FilesDir") || name_has(id->name, "RootPath") ||
      name_has(id->name, "ObbDir") || name_has(id->name, "AssetPath") ||
      name_has(id->name, "Path"))
    return jni_make_string(managed_path(data_dir()));
  if (is_editbox_text(id->name))
    return jni_make_string(editbox_text());
  /* Non-null package objects expose version fields. */
  if (name_has(id->name, "getPackageInfo"))     return jni_make_object("android/content/pm/PackageInfo");
  if (name_has(id->name, "getApplicationInfo")) return jni_make_object("android/content/pm/ApplicationInfo");
  if (name_has(id->name, "getPackageManager"))  return jni_make_object("android/content/pm/PackageManager");
  if (name_has(id->name, "getResources"))       return jni_make_object("android/content/res/Resources");
  if (name_has(id->name, "getConfiguration"))   return jni_make_object("android/content/res/Configuration");
  /* Locale.getDefault() must return an object. */
  if (sig_returns(id->sig, "Ljava/util/Locale;"))
    return jni_make_object("java/util/Locale");
  if (sig_returns(id->sig, "Ljava/lang/String;"))
    return jni_make_string(""); // UUID, asset-pack name, etc.
  (void)va;
  return NULL;
}

static juint act_int(const FakeID *id, va_list va) {
  if (name_has(id->cls, "NativeGallery") &&
      (!strcmp(id->name, "CheckPermission") ||
       !strcmp(id->name, "RequestPermission")))
    return 1;
  /* Java wall and monotonic clocks. */
  if (!strcmp(id->name, "currentTimeMillis") ||
      !strcmp(id->name, "elapsedRealtime") || !strcmp(id->name, "uptimeMillis") ||
      !strcmp(id->name, "nanoTime") || !strcmp(id->name, "elapsedRealtimeNanos")) {
    struct timespec ts;
    const int wall = !strcmp(id->name, "currentTimeMillis");
    clock_gettime(wall ? CLOCK_REALTIME : CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
    return (!strcmp(id->name, "nanoTime") || !strcmp(id->name, "elapsedRealtimeNanos"))
             ? ns : ns / UINT64_C(1000000);
  }
  /* FMOD parses audio properties through these methods. */
  if (name_has(id->name, "parseInt") || name_has(id->name, "parseLong")) {
    const char *s = first_string_arg(id->sig, va);
    return (juint)(s ? strtol(s, NULL, 10) : 0);
  }
  if (is_editbox_open(id->name)) return (juint)editbox_is_open();
  /* Some keyboard methods return a success flag. */
  if (is_editbox_show(id->name)) { editbox_show(first_string_arg(id->sig, va), 32); return 1; }
  /* Play Asset Delivery is unavailable. */
  if (name_has(id->name, "playCoreApiMissing")) return 1;
  /* ConnectionResult.SERVICE_MISSING. */
  if (name_has(id->name, "isGooglePlayServicesAvailable")) return 1; /* ConnectionResult.SERVICE_MISSING */
  /* Camera and AR services are unavailable. */
  if (name_has(id->name, "hasSystemFeature") ||
      name_has(id->name, "isCameraAvailable") ||
      name_has(id->name, "isArCoreSupported") ||
      name_has(id->name, "checkAvailability")) return 0;
  if (name_has(id->name, "getNumberOfCameras")) return 0;
  (void)va;
  return 0;
}

static float act_float(const FakeID *id, va_list va) {
  (void)va;
  float x, y, z;
  android_get_orientation(&x, &y, &z);
  if (name_has(id->name, "OrientationX")) return x;
  if (name_has(id->name, "OrientationY")) return y;
  if (name_has(id->name, "OrientationZ")) return z;
  return 0.0f;
}

static void act_void(const FakeID *id, va_list va) {
  if (is_editbox_show(id->name)) { editbox_show(first_string_arg(id->sig, va), 32); return; }
  if (is_editbox_close(id->name)) { editbox_close(); return; }
  (void)va;
  if (!strcmp(id->name, "finish") || name_has(id->name, "appEnd") ||
      name_has(id->name, "exitApp")) {
    return;
  }
}

/* Delegate Unity/input classes to the concrete Pocket Camp host modules. */
#include "unity_jni.h"
#include "unity_input.h"

static void *dispatch_object(void *recv, const FakeID *id, va_list va) {
  /* Route Locale methods by receiver label. */
  if (recv_has_label(recv, "java/util/Locale")) {
    void *r = locale_object_result(id->name);
    if (r) return r;
  }
  /* Copy MotionEvent handles. */
  if (input_owns_class(id->cls) && !strcmp(id->name, "obtain") &&
      strstr(id->sig, "(Landroid/view/MotionEvent;)")) {
    void *src = va_arg(va, void *);
    return unity_motionevent_obtain(src);
  }
  /* String.getBytes() for PlayerPrefs key encoding. */
  if (recv && *(uint32_t *)recv == TAG_STRING && name_has(id->name, "getBytes")) {
    const char *u = ((FakeString *)recv)->utf; int n = (int)strlen(u);
    char *d = malloc(n > 0 ? n : 1); if (n) memcpy(d, u, n);
    return make_pri_array_adopt(d, n, 1);
  }
  if (unity_owns_class(id->cls)) return unity_dispatch_object(recv, id, va);
  return act_object(id, va);
}
static juint dispatch_int(void *recv, const FakeID *id, va_list va) {
  /* Route String methods by receiver tag. */
  if (recv && *(uint32_t *)recv == TAG_STRING) {
    if (!strcmp(id->name, "length"))   return utf16_len(((FakeString *)recv)->utf);
    if (!strcmp(id->name, "hashCode")) return 0;
    if (!strcmp(id->name, "isEmpty"))  return ((FakeString *)recv)->utf[0] == '\0';
  }
  /* Unbox PlayerPrefs values by receiver. */
  if (unity_is_boxed(recv)) return unity_boxed_int(recv);
  /* Route input getters by receiver tag. */
  if (input_owns_recv(recv)) return input_dispatch_int(recv, id, va);
  if (unity_owns_class(id->cls)) return unity_dispatch_int(recv, id, va);
  if (input_owns_class(id->cls)) return input_dispatch_int(recv, id, va);
  return act_int(id, va);
}
static float dispatch_float(void *recv, const FakeID *id, va_list va) {
  if (unity_is_boxed(recv)) return unity_boxed_float(recv);   /* Float.floatValue */
  if (input_owns_recv(recv)) return input_dispatch_float(recv, id, va);
  if (input_owns_class(id->cls)) return input_dispatch_float(recv, id, va);
  return act_float(id, va);
}
static void dispatch_void(void *recv, const FakeID *id, va_list va) {
  if (unity_owns_class(id->cls)) { unity_dispatch_void(recv, id, va); return; }
  act_void(id, va);
}

static juint j_GetVersion(void *env) { (void)env; return JNI_VERSION_1_6; }
static void *j_FindClass(void *env, const char *name) {
  (void)env;
  return intern_class(name ? name : "?");
}
static void *j_GetObjectClass(void *env, void *obj) {
  (void)env;
  /* Preserve concrete labels for pooled objects. */
  if (obj && *(uint32_t *)obj == TAG_CLASS) {
    if ((FakeClass *)obj >= class_pool && (FakeClass *)obj < class_pool + MAX_CLASSES)
      return intern_class("java/lang/Class");
    const FakeObject *o = (const FakeObject *)obj;
    if (o->label[0]) return intern_class(o->label);
  }
  if (obj && *(uint32_t *)obj == TAG_OBJECT) {
    const FakeObject *o = (const FakeObject *)obj;
    if (o->label[0]) return intern_class(o->label);
  }
  return intern_class("java/lang/Object");
}
static void *j_GetMethodID(void *env, void *cls, const char *name, const char *sig) {
  (void)env; return get_id(class_name_of(cls), name ? name : "", sig ? sig : "");
}
static void *j_GetFieldID(void *env, void *cls, const char *name, const char *sig) {
  (void)env; return get_id(class_name_of(cls), name ? name : "", sig ? sig : "");
}

/* Decode String(byte[][,charset]) as UTF-8. */
static void *new_object_dispatch(void *cls, void *mid, void *first_arg) {
  const char *cn = class_name_of(cls);
  if (cn && strstr(cn, "java/lang/String")) {
    FakeID *m = mid;
    if (m && strstr(m->sig, "[B")) {              /* String([B...) */
      int len = 0; char *b = jni_bytearray_data(first_arg, &len);
      if (b && len > 0) { char *t = malloc(len + 1); memcpy(t, b, len); t[len] = 0;
        void *s = jni_make_string(t); free(t); return s; }
      return jni_make_string("");
    }
  }
  return jni_make_object(cn);
}

static void *j_NewObject(void *env, void *cls, void *mid, ...) {
  (void)env;
  va_list va; va_start(va, mid); void *a0 = va_arg(va, void *); va_end(va);
  return new_object_dispatch(cls, mid, a0);
}
static void *j_NewObjectV(void *env, void *cls, void *mid, va_list va) {
  (void)env; void *a0 = va_arg(va, void *);
  return new_object_dispatch(cls, mid, a0);
}

static void *j_NewGlobalRef(void *env, void *obj) {
  (void)env;
  mutexLock(&locals_lock);
  for (int i = locals_top - 1; i >= 0; i--)
    if (locals[i] == obj) { locals[i] = locals[--locals_top]; break; }
  mutexUnlock(&locals_lock);
  return obj;
}
static void j_DeleteGlobalRef(void *env, void *obj) { (void)env; free_ref(obj); }
static void j_DeleteLocalRef(void *env, void *obj) { (void)env; delete_local(obj); }
static void *j_NewLocalRef(void *env, void *obj) { (void)env; return obj; }
static juint j_IsSameObject(void *env, void *a, void *b) { (void)env; return a == b; }

/* Exact input/boxed checks; opaque handles accept other casts. */
static juint j_IsInstanceOf(void *env, void *obj, void *clazz) {
  (void)env;
  const char *cn = class_name_of(clazz);
  /* Classify input events by handle kind. */
  if (input_owns_recv(obj)) {
    if (strstr(cn, "MotionEvent")) return input_recv_is_motion(obj) ? 1 : 0;
    if (strstr(cn, "KeyEvent"))    return input_recv_is_motion(obj) ? 0 : 1;
    /* Both event kinds derive from InputEvent. */
    return 1;
  }
  /* Preserve boxed PlayerPrefs value types. */
  int ui = unity_isinstance(obj, cn);
  if (ui >= 0) return (juint)ui;
  if (obj && *(uint32_t *)obj == TAG_STRING) {
    if (strstr(cn, "String")) return 1;
    if (strstr(cn, "Integer") || strstr(cn, "Long") || strstr(cn, "Float") ||
        strstr(cn, "Double")  || strstr(cn, "Boolean") || strstr(cn, "Character") ||
        strstr(cn, "Short")   || strstr(cn, "Byte"))
      return 0;
    /* Other opaque classes accept the cast. */
  }
  return 1;
}
static juint j_EnsureLocalCapacity(void *env, int cap) { (void)env; (void)cap; return 0; }

static juint j_PushLocalFrame(void *env, int cap) {
  (void)env; (void)cap;
  mutexLock(&locals_lock);
  if (frame_top < MAX_FRAMES)
    frames[frame_top++] = locals_top;
  mutexUnlock(&locals_lock);
  return 0;
}
static void *j_PopLocalFrame(void *env, void *result) {
  (void)env;
  mutexLock(&locals_lock);
  const int mark = frame_top > 0 ? frames[--frame_top] : 0;
  for (int i = mark; i < locals_top; i++)
    if (locals[i] != result)
      free_ref(locals[i]);
  locals_top = mark;
  if (result && locals_top < MAX_LOCALS)
    locals[locals_top++] = result;
  mutexUnlock(&locals_lock);
  return result;
}


#define CALL_VARIADIC(fn, ret_t, dispatch) \
  static ret_t fn(void *env, void *recv, FakeID *id, ...) { \
    (void)env; va_list va; va_start(va, id); \
    ret_t r = dispatch(recv, id, va); va_end(va); return r; } \
  static ret_t fn##V(void *env, void *recv, FakeID *id, va_list va) { \
    (void)env; return dispatch(recv, id, va); }

CALL_VARIADIC(j_CallObjectMethod, void *, dispatch_object)
CALL_VARIADIC(j_CallIntMethod, juint, dispatch_int)
CALL_VARIADIC(j_CallBooleanMethod, juint, dispatch_int)
CALL_VARIADIC(j_CallLongMethod, juint, dispatch_int)
CALL_VARIADIC(j_CallFloatMethod, float, dispatch_float)

static void j_CallVoidMethod(void *env, void *recv, FakeID *id, ...) {
  (void)env; va_list va; va_start(va, id); dispatch_void(recv, id, va); va_end(va);
}
static void j_CallVoidMethodV(void *env, void *recv, FakeID *id, va_list va) {
  (void)env; dispatch_void(recv, id, va);
}

#define j_CallStaticObjectMethod   j_CallObjectMethod
#define j_CallStaticObjectMethodV  j_CallObjectMethodV
#define j_CallStaticIntMethod      j_CallIntMethod
#define j_CallStaticIntMethodV     j_CallIntMethodV
#define j_CallStaticBooleanMethod  j_CallBooleanMethod
#define j_CallStaticBooleanMethodV j_CallBooleanMethodV
#define j_CallStaticLongMethod     j_CallLongMethod
#define j_CallStaticLongMethodV    j_CallLongMethodV
#define j_CallStaticFloatMethod    j_CallFloatMethod
#define j_CallStaticFloatMethodV   j_CallFloatMethodV
#define j_CallStaticVoidMethod     j_CallVoidMethod
#define j_CallStaticVoidMethodV    j_CallVoidMethodV

static void *j_CallObjectMethodA (void *e, void *r, FakeID *id, const void *a){
  if (a && is_reflection_helper(id)) {
    void *result = reflection_object_a(id, a);
    if (result) return result;
  }
  if (a && name_has(id->cls, "NativeGallery") && !strcmp(id->name, "SaveMedia"))
    return native_gallery_save(((void *const *)a)[2]);
  /* Preserve AudioManager.getProperty()'s jvalue argument. */
  if (a && name_has(id->name, "getProperty"))
    return getproperty_value(jni_string_utf(((void *const *)a)[0]));
  if (a && name_has(id->cls, "net/Uri") && (name_has(id->name, "encode") || name_has(id->name, "decode")))
    return (void *)((void *const *)a)[0];        /* identity, jvalue path */
  return j_CallObjectMethod(e, r, id);
}
static juint j_CallBooleanMethodA(void *e, void *r, FakeID *id, const void *a){ (void)a; return j_CallBooleanMethod(e, r, id); }
static juint j_CallIntMethodA    (void *e, void *r, FakeID *id, const void *a){
  /* parseInt/parseLong receive the String in jvalue[0]. */
  if (a && (name_has(id->name, "parseInt") || name_has(id->name, "parseLong"))) {
    const char *s = jni_string_utf(((void *const *)a)[0]);
    return (juint)(s ? strtol(s, NULL, 10) : 0);
  }
  (void)a; return j_CallIntMethod(e, r, id);
}
static juint j_CallLongMethodA   (void *e, void *r, FakeID *id, const void *a){ (void)a; return j_CallLongMethod(e, r, id); }
static float j_CallFloatMethodA  (void *e, void *r, FakeID *id, const void *a){ (void)a; return j_CallFloatMethod(e, r, id); }
static void  j_CallVoidMethodA   (void *e, void *r, FakeID *id, const void *a){ (void)a; j_CallVoidMethod(e, r, id); }
static void *j_NewObjectA        (void *e, void *cls, void *mid, const void *a){ (void)e;
  return new_object_dispatch(cls, mid, a ? ((void *const *)a)[0] : NULL); }
#define j_CallStaticObjectMethodA  j_CallObjectMethodA
#define j_CallStaticBooleanMethodA j_CallBooleanMethodA
#define j_CallStaticIntMethodA     j_CallIntMethodA
#define j_CallStaticLongMethodA    j_CallLongMethodA
#define j_CallStaticFloatMethodA   j_CallFloatMethodA
#define j_CallStaticVoidMethodA    j_CallVoidMethodA


static void *j_NewStringUTF(void *env, const char *utf) { (void)env; return jni_make_string(utf); }
static void *j_NewString(void *env, const uint16_t *u, int len) {
  (void)env;
  if (!u || len < 0) return jni_make_string("");
  char *tmp = malloc((size_t)len * 4 + 1);
  int o = 0;
  for (int i = 0; i < len; i++) { // naive UTF-16 -> UTF-8 (BMP)
    const uint32_t c = u[i];
    if (c < 0x80) tmp[o++] = (char)c;
    else if (c < 0x800) { tmp[o++] = 0xC0 | (c >> 6); tmp[o++] = 0x80 | (c & 0x3F); }
    else { tmp[o++] = 0xE0 | (c >> 12); tmp[o++] = 0x80 | ((c >> 6) & 0x3F); tmp[o++] = 0x80 | (c & 0x3F); }
  }
  tmp[o] = 0;
  void *s = jni_make_string(tmp);
  free(tmp);
  return s;
}
static const char *j_GetStringUTFChars(void *env, void *jstr, uint8_t *is_copy) {
  (void)env; if (is_copy) *is_copy = 0; return obj_str(jstr);
}
static void j_ReleaseStringUTFChars(void *env, void *jstr, const char *utf) { (void)env; (void)jstr; (void)utf; }
static juint j_GetStringUTFLength(void *env, void *jstr) { (void)env; return strlen(obj_str(jstr)); }

/* Copy a modified UTF-8 string region. */
static void j_GetStringUTFRegion(void *env, void *jstr, int start, int len, char *buf) {
  (void)env;
  if (!buf) return;
  const char *s = obj_str(jstr);
  const int slen = (int)strlen(s);
  if (start < 0) start = 0;
  if (start > slen) start = slen;
  if (len < 0) len = 0;
  if (start + len > slen) len = slen - start;
  memcpy(buf, s + start, (size_t)len);
  buf[len] = '\0';
}
/* Copy a UTF-16 string region. */
static void j_GetStringRegion(void *env, void *jstr, int start, int len, uint16_t *buf) {
  (void)env;
  if (!buf) return;
  const char *s = obj_str(jstr);
  const int slen = (int)strlen(s);
  if (start < 0) start = 0;
  if (start > slen) start = slen;
  if (len < 0) len = 0;
  if (start + len > slen) len = slen - start;
  for (int i = 0; i < len; i++) buf[i] = (uint8_t)s[start + i];
}
/* Java string length uses UTF-16 code units. */
static juint j_GetStringLength(void *env, void *jstr) {
  (void)env;
  return utf16_len(obj_str(jstr));
}


static juint j_GetArrayLength(void *env, void *arr) {
  (void)env;
  FakeObjArray *a = arr;
  if (a && (a->tag == TAG_PRIARR || a->tag == TAG_OBJARR))
    return a->len;
  return 0;
}

static void *new_pri_array(int len, int elem_size) {
  void *data = calloc(len ? len : 1, elem_size);
  return make_pri_array_adopt(data, len, elem_size);
}
static void *j_NewByteArray(void *env, int len) { (void)env; return new_pri_array(len, 1); }
static void *j_NewIntArray(void *env, int len) { (void)env; return new_pri_array(len, 4); }
static void *j_NewFloatArray(void *env, int len) { (void)env; return new_pri_array(len, 4); }

static void *j_NewObjectArray(void *env, int len, void *cls, void *init) {
  (void)env; (void)cls;
  FakeObjArray *a = calloc(1, sizeof(*a));
  a->tag = TAG_OBJARR;
  a->len = len;
  a->items = calloc(len ? len : 1, sizeof(void *));
  for (int i = 0; i < len; i++) a->items[i] = init;
  return reg_local(a);
}
static void *j_GetObjectArrayElement(void *env, void *arr, int i) {
  (void)env;
  FakeObjArray *a = arr;
  return (a && a->tag == TAG_OBJARR && i >= 0 && i < a->len) ? a->items[i] : NULL;
}
static void j_SetObjectArrayElement(void *env, void *arr, int i, void *val) {
  (void)env;
  FakeObjArray *a = arr;
  if (a && a->tag == TAG_OBJARR && i >= 0 && i < a->len) a->items[i] = val;
}

static void *j_GetPriArrayElements(void *env, void *arr, uint8_t *is_copy) {
  (void)env; if (is_copy) *is_copy = 0;
  FakePriArray *a = arr;
  return (a && a->tag == TAG_PRIARR) ? a->data : NULL;
}
static void j_ReleasePriArrayElements(void *env, void *arr, void *elems, int mode) {
  (void)env; (void)arr; (void)elems; (void)mode;
}
static void j_GetPriArrayRegion(void *env, void *arr, int start, int len, void *buf) {
  (void)env;
  FakePriArray *a = arr;
  if (a && a->tag == TAG_PRIARR && start >= 0 && start + len <= a->len)
    memcpy(buf, (char *)a->data + (size_t)start * a->elem_size, (size_t)len * a->elem_size);
}
static void j_SetPriArrayRegion(void *env, void *arr, int start, int len, const void *buf) {
  (void)env;
  FakePriArray *a = arr;
  if (a && a->tag == TAG_PRIARR && start >= 0 && start + len <= a->len)
    memcpy((char *)a->data + (size_t)start * a->elem_size, buf, (size_t)len * a->elem_size);
}

/* Android framework fields. */
#define APP_VERSION_NAME ACPC_VERSION_NAME
#define APP_VERSION_CODE ACPC_VERSION_CODE
#define NX_SDK_INT       33        /* Android 13 -- high enough to pass any minSdk gate  */

static int fld_is(const FakeID *id, const char *cls_sub, const char *name) {
  return name_has(id->cls, cls_sub) && !strcmp(id->name, name);
}

static void *field_object(const FakeID *id) {
  const char *n = id->name, *c = id->cls;
  if (!strcmp(n, "versionName")) return jni_make_string(APP_VERSION_NAME);
  /* UnityPlayer.currentActivity. */
  if (name_has(c, "unity3d/player/UnityPlayer")) {
    if (!strcmp(n, "currentActivity")) return jni_make_object("android/app/Activity");
    if (!strcmp(n, "MANUFACTURER"))    return jni_make_string("Nintendo");
  }
  /* Track the AudioManager property requested by FMOD. */
  if (name_has(c, "media/AudioManager")) {
    if (!strcmp(n, "PROPERTY_OUTPUT_FRAMES_PER_BUFFER")) { g_last_output_prop = 2; return jni_make_string("android.media.property.OUTPUT_FRAMES_PER_BUFFER"); }
    if (!strcmp(n, "PROPERTY_OUTPUT_SAMPLE_RATE"))       { g_last_output_prop = 1; return jni_make_string("android.media.property.OUTPUT_SAMPLE_RATE"); }
  }
  if (name_has(c, "content/Context")) {
    if (!strcmp(n, "AUDIO_SERVICE"))        return jni_make_string("audio");
    if (!strcmp(n, "DISPLAY_SERVICE"))      return jni_make_string("display");
    if (!strcmp(n, "WINDOW_SERVICE"))       return jni_make_string("window");
    if (!strcmp(n, "LOCATION_SERVICE"))     return jni_make_string("location");
    if (!strcmp(n, "CONNECTIVITY_SERVICE")) return jni_make_string("connectivity");
    if (!strcmp(n, "MEDIA_ROUTER_SERVICE")) return jni_make_string("media_router");
    if (!strcmp(n, "VIBRATOR_SERVICE"))     return jni_make_string("vibrator");
  }
  /* Match getExternalStorageState(). */
  if (name_has(c, "os/Environment")) {
    if (!strcmp(n, "MEDIA_MOUNTED"))           return jni_make_string("mounted");
    if (!strcmp(n, "MEDIA_MOUNTED_READ_ONLY")) return jni_make_string("mounted_ro");
  }
  if (name_has(c, "pm/PackageManager")) {
    if (!strcmp(n, "FEATURE_AUDIO_LOW_LATENCY")) return jni_make_string("android.hardware.audio.low_latency");
    if (!strcmp(n, "FEATURE_AUDIO_PRO"))         return jni_make_string("android.hardware.audio.pro");
  }
  if (name_has(c, "provider/Settings$Secure") && !strcmp(n, "ANDROID_ID"))
    return jni_make_string("android_id");
  if (name_has(c, "os/Build")) {
    if (!strcmp(n, "MODEL"))        return jni_make_string("Switch");
    if (!strcmp(n, "MANUFACTURER")) return jni_make_string("Nintendo");
    if (!strcmp(n, "BRAND"))        return jni_make_string("Nintendo");
    if (!strcmp(n, "DEVICE"))       return jni_make_string("Switch");
    if (!strcmp(n, "PRODUCT"))      return jni_make_string("Switch");
    if (!strcmp(n, "HARDWARE"))     return jni_make_string("nx");
    if (!strcmp(n, "BOARD"))        return jni_make_string("nx");
    if (!strcmp(n, "DISPLAY"))      return jni_make_string("nx");
    if (!strcmp(n, "ID"))           return jni_make_string("REL");
    if (!strcmp(n, "TYPE"))         return jni_make_string("user");
    if (!strcmp(n, "TAGS"))         return jni_make_string("release-keys");
    if (!strcmp(n, "FINGERPRINT"))  return jni_make_string("Nintendo/Switch/Switch:13/REL/51472:user/release-keys");
    if (!strcmp(n, "BOOTLOADER"))   return jni_make_string("unknown");
    if (!strcmp(n, "HOST"))         return jni_make_string("localhost");
    if (!strcmp(n, "USER"))         return jni_make_string("nx");
    if (!strcmp(n, "SERIAL"))       return jni_make_string("unknown");
    if (!strcmp(n, "RELEASE"))      return jni_make_string("13");        /* Build.VERSION.* */
    if (!strcmp(n, "CODENAME"))     return jni_make_string("REL");
    if (!strcmp(n, "INCREMENTAL"))  return jni_make_string("51472");
    if (!strcmp(n, "SECURITY_PATCH")) return jni_make_string("2023-01-01");
    if (!strcmp(n, "BASE_OS"))      return jni_make_string("");
  }
  /* Other String fields remain non-null. */
  if (sig_returns(id->sig, "Ljava/lang/String;")) return jni_make_string("");
  return NULL;
}

static juint field_int(const FakeID *id) {
  const char *n = id->name, *c = id->cls;
  if (!strcmp(n, "versionCode")) return APP_VERSION_CODE;
  if (name_has(c, "unity3d/player/UnityPlayer")) {
    if (!strcmp(n, "SDK_INT"))     return NX_SDK_INT;
    if (!strcmp(n, "densityDpi"))  return 320;
    if (!strcmp(n, "widthPixels")) return (juint)screen_width;
    if (!strcmp(n, "heightPixels"))return (juint)screen_height;
    if (!strcmp(n, "STREAM_MUSIC"))return 3;   /* AudioManager.STREAM_MUSIC      */
    if (!strcmp(n, "GET_DEVICES_OUTPUTS")) return 2; /* AudioManager.GET_DEVICES_OUTPUTS */
    if (!strcmp(n, "ROUTE_TYPE_LIVE_VIDEO")) return 1;
    if (!strcmp(n, "SCREEN_ORIENTATION_UNSPECIFIED"))       return -1;
    if (!strcmp(n, "SCREEN_ORIENTATION_LANDSCAPE"))         return 0;
    if (!strcmp(n, "SCREEN_ORIENTATION_PORTRAIT"))          return 1;
    if (!strcmp(n, "SCREEN_ORIENTATION_REVERSE_LANDSCAPE")) return 8;
    if (!strcmp(n, "SCREEN_ORIENTATION_REVERSE_PORTRAIT"))  return 9;
    if (!strcmp(n, "SCREEN_ORIENTATION_FULL_USER"))         return 13;
    if (!strcmp(n, "SCREEN_ORIENTATION_FULL_SENSOR"))       return 10;
  }
  if (name_has(c, "content/Context") && !strcmp(n, "MODE_PRIVATE")) return 0;
  if (name_has(c, "pm/PackageManager")) {
    if (!strcmp(n, "PERMISSION_GRANTED")) return 0;   /* == granted              */
    if (!strcmp(n, "PERMISSION_DENIED"))  return (juint)-1;
  }
  if (name_has(c, "os/Build")) {
    if (!strcmp(n, "SDK_INT"))          return NX_SDK_INT;
    if (!strcmp(n, "PREVIEW_SDK_INT"))  return 0;
  }
  if (name_has(c, "DisplayMetrics")) {
    if (!strcmp(n, "widthPixels"))  return (juint)screen_width;
    if (!strcmp(n, "heightPixels")) return (juint)screen_height;
    if (!strcmp(n, "densityDpi"))   return 320;    /* xhdpi bucket                */
  }
  return 0;
}

/* xhdpi DisplayMetrics values. */
static float field_float(const FakeID *id) {
  const char *n = id->name;
  if (name_has(id->cls, "DisplayMetrics")) {
    if (!strcmp(n, "density") || !strcmp(n, "scaledDensity")) return 2.0f;
    if (!strcmp(n, "xdpi") || !strcmp(n, "ydpi"))             return 320.0f;
  }
  (void)fld_is;
  return 0.0f;
}

static void *j_GetObjectField(void *env, void *obj, void *fid) {
  (void)env; (void)obj; if (!fid) return NULL;
  return field_object((const FakeID *)fid); }
static juint j_GetIntField(void *env, void *obj, void *fid) {
  (void)env; (void)obj; if (!fid) return 0;
  return field_int((const FakeID *)fid); }
static juint j_GetLongField(void *env, void *obj, void *fid) {
  (void)env; (void)obj; if (!fid) return 0; return (juint)field_int((const FakeID *)fid); }
static juint j_GetBooleanField(void *env, void *obj, void *fid) {
  (void)env; (void)obj; if (!fid) return 0; return field_int((const FakeID *)fid) ? 1 : 0; }
static float j_GetFloatField(void *env, void *obj, void *fid) {
  (void)env; (void)obj; if (!fid) return 0.0f; return field_float((const FakeID *)fid); }

static void *j_FromReflectedMethod(void *env, void *m) {
  (void)env;
  if (m && *(uint32_t *)m == TAG_ID) return m;
  return get_id("java/lang/reflect/Method", "invoke", "()V"); }
static void *j_FromReflectedField(void *env, void *f) {
  (void)env;
  if (f && *(uint32_t *)f == TAG_ID) return f;
  return get_id("java/lang/reflect/Field", "field", "()V"); }
static void *j_ToReflectedMethod(void *env, void *cls, void *mid, juint isStatic) {
  (void)env; (void)cls; (void)isStatic; return mid ? mid : jni_make_object("java/lang/reflect/Method"); }
static void *j_ToReflectedField(void *env, void *cls, void *fid, juint isStatic) {
  (void)env; (void)cls; (void)isStatic; return fid ? fid : jni_make_object("java/lang/reflect/Field"); }


static juint j_RegisterNatives(void *env, void *cls, void *methods, int n) {
  (void)env; (void)cls; (void)methods; (void)n;
  return 0;
}
static juint j_GetJavaVM(void *env, void **vm) { (void)env; *vm = fake_vm; return JNI_OK; }
static juint j_ExceptionCheck(void *env) { (void)env; return 0; }
static void *j_ExceptionOccurred(void *env) { (void)env; return NULL; }
static void j_void1(void *env) { (void)env; }

static unsigned char g_direct_buffer[64 * 1024];
static void *j_GetDirectBufferAddress(void *env, void *buf) {
  (void)env; (void)buf; return g_direct_buffer;
}
static long j_GetDirectBufferCapacity(void *env, void *buf) {
  (void)env; (void)buf; return (long)sizeof g_direct_buffer;
}

static void *env_table[233];
static void **env_table_ptr = env_table;
/* Accessors for opaque JNI strings and byte arrays. */
void *jni_bytearray_data(void *arr, int *len_out) {
  FakePriArray *a = arr;
  if (a && a->tag == TAG_PRIARR) { if (len_out) *len_out = a->len; return a->data; }
  if (len_out) *len_out = 0;
  return NULL;
}
const char *jni_string_utf(void *jstr) {
  FakeString *s = jstr;
  return (s && s->tag == TAG_STRING) ? s->utf : "";
}

void *fake_env = &env_table_ptr;

static juint vm_DestroyJavaVM(void *vm) { (void)vm; return JNI_OK; }
static juint vm_AttachCurrentThread(void *vm, void **env, void *args) {
  (void)vm; (void)args; if (env) *env = fake_env; return JNI_OK;
}
static juint vm_DetachCurrentThread(void *vm) { (void)vm; return JNI_OK; }
static juint vm_GetEnv(void *vm, void **env, int version) {
  (void)vm; (void)version; if (env) *env = fake_env; return JNI_OK;
}
static void *vm_table[8];
static void **vm_table_ptr = vm_table;
void *fake_vm = &vm_table_ptr;

void jni_init(void) {
  mutexInit(&locals_lock);

  jni_fill_unimpl(env_table);

  env_table[4]   = (void *)j_GetVersion;
  env_table[6]   = (void *)j_FindClass;
  env_table[7]   = (void *)j_FromReflectedMethod;
  env_table[8]   = (void *)j_FromReflectedField;
  env_table[9]   = (void *)j_ToReflectedMethod;
  env_table[12]  = (void *)j_ToReflectedField;
  env_table[15]  = (void *)j_ExceptionOccurred;
  env_table[16]  = (void *)j_void1; // ExceptionDescribe
  env_table[17]  = (void *)j_void1; // ExceptionClear
  env_table[19]  = (void *)j_PushLocalFrame;
  env_table[20]  = (void *)j_PopLocalFrame;
  env_table[21]  = (void *)j_NewGlobalRef;
  env_table[22]  = (void *)j_DeleteGlobalRef;
  env_table[23]  = (void *)j_DeleteLocalRef;
  env_table[24]  = (void *)j_IsSameObject;
  env_table[25]  = (void *)j_NewLocalRef;
  env_table[26]  = (void *)j_EnsureLocalCapacity;
  env_table[28]  = (void *)j_NewObject;
  env_table[29]  = (void *)j_NewObjectV;
  env_table[31]  = (void *)j_GetObjectClass;
  env_table[32]  = (void *)j_IsInstanceOf;
  env_table[33]  = (void *)j_GetMethodID;
  env_table[34]  = (void *)j_CallObjectMethod;
  env_table[35]  = (void *)j_CallObjectMethodV;
  env_table[37]  = (void *)j_CallBooleanMethod;
  env_table[38]  = (void *)j_CallBooleanMethodV;
  env_table[49]  = (void *)j_CallIntMethod;
  env_table[50]  = (void *)j_CallIntMethodV;
  env_table[52]  = (void *)j_CallLongMethod;
  env_table[53]  = (void *)j_CallLongMethodV;
  env_table[55]  = (void *)j_CallFloatMethod;
  env_table[56]  = (void *)j_CallFloatMethodV;
  env_table[61]  = (void *)j_CallVoidMethod;
  env_table[62]  = (void *)j_CallVoidMethodV;
  env_table[30]  = (void *)j_NewObjectA;
  env_table[36]  = (void *)j_CallObjectMethodA;
  env_table[39]  = (void *)j_CallBooleanMethodA;
  env_table[51]  = (void *)j_CallIntMethodA;
  env_table[54]  = (void *)j_CallLongMethodA;
  env_table[57]  = (void *)j_CallFloatMethodA;
  env_table[63]  = (void *)j_CallVoidMethodA;
  env_table[94]  = (void *)j_GetFieldID;
  env_table[95]  = (void *)j_GetObjectField;
  env_table[96]  = (void *)j_GetBooleanField;        // GetBooleanField
  env_table[100] = (void *)j_GetIntField;
  env_table[101] = (void *)j_GetLongField;           // GetLongField
  env_table[102] = (void *)j_GetFloatField;          // GetFloatField
  env_table[113] = (void *)j_GetMethodID;            // GetStaticMethodID
  env_table[114] = (void *)j_CallStaticObjectMethod;
  env_table[115] = (void *)j_CallStaticObjectMethodV;
  env_table[117] = (void *)j_CallStaticBooleanMethod;
  env_table[118] = (void *)j_CallStaticBooleanMethodV;
  env_table[129] = (void *)j_CallStaticIntMethod;
  env_table[130] = (void *)j_CallStaticIntMethodV;
  env_table[132] = (void *)j_CallStaticLongMethod;
  env_table[133] = (void *)j_CallStaticLongMethodV;
  env_table[135] = (void *)j_CallStaticFloatMethod;
  env_table[136] = (void *)j_CallStaticFloatMethodV;
  env_table[141] = (void *)j_CallStaticVoidMethod;
  env_table[142] = (void *)j_CallStaticVoidMethodV;
  env_table[116] = (void *)j_CallStaticObjectMethodA;
  env_table[119] = (void *)j_CallStaticBooleanMethodA;
  env_table[131] = (void *)j_CallStaticIntMethodA;
  env_table[134] = (void *)j_CallStaticLongMethodA;
  env_table[137] = (void *)j_CallStaticFloatMethodA;
  env_table[143] = (void *)j_CallStaticVoidMethodA;
  env_table[144] = (void *)j_GetFieldID;             // GetStaticFieldID
  env_table[145] = (void *)j_GetObjectField;         // GetStaticObjectField
  env_table[146] = (void *)j_GetBooleanField;        // GetStaticBooleanField
  env_table[150] = (void *)j_GetIntField;            // GetStaticIntField
  env_table[151] = (void *)j_GetLongField;           // GetStaticLongField
  env_table[152] = (void *)j_GetFloatField;          // GetStaticFloatField
  env_table[163] = (void *)j_NewString;
  env_table[164] = (void *)j_GetStringLength;
  env_table[167] = (void *)j_NewStringUTF;
  env_table[168] = (void *)j_GetStringUTFLength;
  env_table[169] = (void *)j_GetStringUTFChars;
  env_table[170] = (void *)j_ReleaseStringUTFChars;
  env_table[171] = (void *)j_GetArrayLength;
  env_table[172] = (void *)j_NewObjectArray;
  env_table[173] = (void *)j_GetObjectArrayElement;
  env_table[174] = (void *)j_SetObjectArrayElement;
  env_table[176] = (void *)j_NewByteArray;
  env_table[179] = (void *)j_NewIntArray;
  env_table[181] = (void *)j_NewFloatArray;
  for (int i = 183; i <= 190; i++) env_table[i] = (void *)j_GetPriArrayElements;
  for (int i = 191; i <= 198; i++) env_table[i] = (void *)j_ReleasePriArrayElements;
  for (int i = 199; i <= 206; i++) env_table[i] = (void *)j_GetPriArrayRegion;
  for (int i = 207; i <= 214; i++) env_table[i] = (void *)j_SetPriArrayRegion;
  env_table[215] = (void *)j_RegisterNatives;
  env_table[219] = (void *)j_GetJavaVM;
  env_table[220] = (void *)j_GetStringRegion;
  env_table[221] = (void *)j_GetStringUTFRegion; // engine reads every string via this
  env_table[222] = (void *)j_GetPriArrayElements;     // GetPrimitiveArrayCritical
  env_table[223] = (void *)j_ReleasePriArrayElements; // ReleasePrimitiveArrayCritical
  env_table[226] = (void *)j_NewGlobalRef;            // NewWeakGlobalRef
  env_table[227] = (void *)j_DeleteGlobalRef;         // DeleteWeakGlobalRef
  env_table[228] = (void *)j_ExceptionCheck;
  env_table[230] = (void *)j_GetDirectBufferAddress;  // fmodProcess drains via this
  env_table[231] = (void *)j_GetDirectBufferCapacity;

  vm_table[3] = (void *)vm_DestroyJavaVM;
  vm_table[4] = (void *)vm_AttachCurrentThread;
  vm_table[5] = (void *)vm_DetachCurrentThread;
  vm_table[6] = (void *)vm_GetEnv;
  vm_table[7] = (void *)vm_AttachCurrentThread; // AttachCurrentThreadAsDaemon
}
