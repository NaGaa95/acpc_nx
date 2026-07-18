/* Firebase SWIG stubs for a host without Google Play Services. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Shared non-null backing for opaque SWIG handles. */
static uint8_t g_fb_obj[1024] __attribute__((aligned(16)));

static long  fb_stub_zero(void)   { return 0; }
static void *fb_stub_handle(void) { return g_fb_obj; }

/* SWIG frees returned strings, so allocate the default app name per call. */
static void *fb_stub_default_name(void) {
  static const char name[] = "__FIRAPP_DEFAULT";
  char *p = (char *)malloc(sizeof(name));
  if (p) memcpy(p, name, sizeof(name));
  return p;
}

static int fb_is_firebase_symbol(const char *s) {
  if (!s) return 0;
  if (strstr(s, "_CSharp_"))                 return 1;  /* Firebase_<Mod>_CSharp_* */
  if (!strncmp(s, "Firebase_", 9))           return 1;
  if (!strncmp(s, "SWIGRegister", 12))       return 1;  /* exception/string cb reg */
  if (!strncmp(s, "SWIG", 4) && strstr(s, "Firebase")) return 1;
  return 0;
}

/* Identify SWIG APIs returning opaque handles. */
static int fb_returns_handle(const char *s) {
  /* Future status and error zero mean complete and successful. */
  if (strstr(s, "GetStatus"))      return 0;
  if (strstr(s, "GetError"))       return 0;
  if (strstr(s, "FutureBase_status")) return 0;
  if (strstr(s, "FutureBase_error"))  return 0;   /* incl. error_message: null is fine when error==0 */
  if (strstr(s, "new_"))            return 1;
  if (strstr(s, "Create"))         return 1;   /* CreateInternal / Create__SWIG_* */
  if (strstr(s, "GetInstance"))    return 1;
  if (strstr(s, "DefaultInstance"))return 1;
  if (strstr(s, "Instance"))       return 1;   /* *_Instance, GetInstanceInternal */
  if (strstr(s, "App_get"))        return 1;   /* RemoteConfig/Messaging .App -> the app object */
  if (strstr(s, "GetReference"))   return 1;
  if (strstr(s, "Future"))         return 1;   /* future handle objects */
  if (strstr(s, "SWIGUpcast"))     return 1;   /* base-class pointer cast */
  if (strstr(s, "GetTask"))        return 1;
  return 0;
}

/* Resolve Firebase SWIG symbols after normal dlsym lookup fails. */
void *firebase_stub_lookup(const char *symbol) {
  if (!fb_is_firebase_symbol(symbol)) return NULL;
  /* FirebaseApp name getters return the default app name. */
  if (strstr(symbol, "DefaultName") ||
      strstr(symbol, "NameInternal") ||
      strstr(symbol, "_Name_get")    ||
      strstr(symbol, "get_Name")) {
    return (void *)&fb_stub_default_name;
  }
  if (fb_returns_handle(symbol)) {
    return (void *)&fb_stub_handle;
  }
  return (void *)&fb_stub_zero;
}
