/* Pocket Camp Complete Unity wrapper entry point. */

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>
#include <SDL2/SDL.h>

#include "config.h"
#include "util.h"
#include "error.h"
#include "so_util.h"
#include "imports.h"
#include "jni_fake.h"
#include "android_native_unity.h"
#include "opensles.h"
#include "unity_entrypoints.h"
#include "editbox.h"
#include "cacerts_pem.h"

#define DATA_ROOT  GAME_HOME
#define LIB_MAIN   "libmain.so"
#define LIB_UNITY  "libunity.so"
#define LIB_IL2CPP "libil2cpp.so"
#define LIB_TONE   "libTone.so"

#define ACPC_FMOD_SETOUTPUT_SITE 0x00b5fc0c
#define ACPC_FMOD_SETOUTPUT_FROM 0x2a1503e1u
#define ACPC_FMOD_SETOUTPUT_TO   0x528002c1u

/* Horizon permits CNTPCT_EL0 but not Android's CNTVCT_EL0. */
#define ACPC_TONE_TICK_SITE 0x000cef28
#define ACPC_TONE_TICK_FROM 0xd53be040u
#define ACPC_TONE_TICK_TO   0xd53be020u

#define ACPC_FTT_ENABLE_SITE   0x009d31c0
#define ACPC_FTT_DISABLE_SITE  0x009d3298
#define ACPC_FTT_ENTRY_FROM    0xd10083ffu
#define ACPC_FTT_ENTRY_TO      0xd65f03c0u
#define ACPC_UNITY_TARGET_FRAME_RATE 0x0080ac34u
#define ACPC_IL2CPP_SET_TARGET_FRAME_RATE 0x05629da0u

#define ACPC_UNITYTLS_ERRORSTATE_CREATE 0x00ba5528u
#define ACPC_UNITYTLS_CA_ENTER          0x00ba7088u
#define ACPC_UNITYTLS_CA_EXIT           0x00ba712cu
#define ACPC_UNITYTLS_CA_APPEND_PEM     0x00ba5d70u

#define ACPC_IL2CPP_LOCALE_INIT    0x02e8b700u

#define ACPC_IL2CPP_GALLERY_CHECK_PERMISSION   0x0538d2f8u
#define ACPC_IL2CPP_GALLERY_REQUEST_PERMISSION 0x0538d4ecu
#define ACPC_IL2CPP_GALLERY_SAVE               0x0538dfd0u

#define ACPC_LOCALNOTIFY_INIT       0x02e6d92cu
#define ACPC_LOCALNOTIFY_PERMISSION 0x02e6db50u
#define ACPC_LOCALNOTIFY_REGISTER   0x02e6db98u
#define ACPC_LOCALNOTIFY_CANCEL     0x02e6df4cu
#define ACPC_LOCALNOTIFY_IS_ENABLED 0x02e6e1b0u

#define ACPC_CARD_CAMERA_WAIT       0x03e0fcf8u
#define ACPC_AR_ENTRY_CONFIRM       0x03d00b1cu
#define ACPC_UNITY_EVENT_INVOKE     0x05661d9cu
#define ACPC_GUIDE_SHOW_MOVE        0x02ae2fc8u
#define ACPC_DIALOG_END_LOADING     0x0391f374u
#define ACPC_GAMEOBJECT_SET_ACTIVE  0x05653a8cu
#define ACPC_ANIMATION_SKIP         0x038e2b7cu
#define ACPC_ANIMATION_SAMPLE       0x056224b0u
#define ACPC_LOADING_ICON_FORCE_STOP 0x02dbe218u
#define ACPC_GUIDE_CAPTURE_VENEER   (ACPC_LOCALNOTIFY_INIT + 4u)

#define ACPC_IL2CPP_REACHABILITY     0x05629e40u
#define ACPC_NTP_GET_NOW             0x02e8044cu

#define ACPC_ASSETREV_NEED_FORCE       0x0394594cu
#define ACPC_ASSETCHECK_EXPIRY_MOVE    0x03c100a0u
#define ACPC_ASSETCHECK_FIRST_RUN_FLAG 0x03c11ca4u
#define ACPC_ASSETCHECK_VERSION_BRANCH 0x03c11d78u
#define ACPC_ASSETLOADER_EXISTS_BRANCH 0x0393a320u

#define ACPC_DN_WWW_TIMEOUT        0x0293f658u
#define ACPC_CD_WWW_TIMEOUT        0x03935680u

void unity_environment_init(const char *data_root);

static void *heap_so_base = NULL;
static size_t heap_so_limit = 0;

void  *g_mmap_arena_base = NULL;
size_t g_mmap_arena_size = 0;
static void  *g_oc_pool_base = NULL;
static size_t g_oc_pool_size = 0;
extern int oc_arena_init(void *window, size_t window_bytes, void *pool, size_t pool_bytes);

so_module main_mod, unity_mod, il2cpp_mod, tone_mod;

extern uintptr_t g_il2cpp_base;

static int nx_install_absolute_hook(uint32_t *site,
                                    const uint32_t expected[4],
                                    uintptr_t target) {
  if (memcmp(site, expected, sizeof(uint32_t) * 4) != 0) return 0;
  const uint32_t stub[4] = {
    0x58000050u, 0xd61f0200u,
    (uint32_t)(target & 0xffffffffu),
    (uint32_t)(target >> 32),
  };
  return so_patch_code(site, stub, sizeof stub) == 0;
}

typedef void *(*il2cpp_domain_get_fn)(void);
typedef const void **(*il2cpp_domain_get_assemblies_fn)(const void *, size_t *);
typedef const void *(*il2cpp_assembly_get_image_fn)(const void *);
typedef void *(*il2cpp_class_from_name_fn)(const void *, const char *, const char *);
typedef void *(*il2cpp_class_get_field_from_name_fn)(void *, const char *);
typedef void (*il2cpp_field_static_set_value_fn)(void *, void *);
typedef void *(*il2cpp_string_new_fn)(const char *);

static il2cpp_domain_get_fn                  g_i_domain_get;
static il2cpp_domain_get_assemblies_fn       g_i_domain_get_assemblies;
static il2cpp_assembly_get_image_fn          g_i_assembly_get_image;
static il2cpp_class_from_name_fn              g_i_class_from_name;
static il2cpp_class_get_field_from_name_fn    g_i_class_get_field;
static il2cpp_field_static_set_value_fn       g_i_field_static_set;
static il2cpp_string_new_fn                   g_i_string_new;
static void                                  *g_locale_class;

typedef struct {
  void *klass;
  void *monitor;
  void *bounds;
  size_t length;
  unsigned char data[];
} NxIl2CppByteArray;

typedef struct {
  void *klass;
  void *monitor;
  int32_t length;
  uint16_t chars[];
} NxIl2CppString;

static void nx_gallery_filename(const NxIl2CppString *source, char *out, size_t size) {
  size_t used = 0;
  if (source && source->length > 0 && source->length < 256) {
    for (int32_t i = 0; i < source->length && used + 1 < size; i++) {
      uint16_t c = source->chars[i];
      if (c == '/' || c == '\\') {
        used = 0;
      } else if (c >= 0x20 && c < 0x7f && c != ':' && c != '*' && c != '?' &&
                 c != '"' && c != '<' && c != '>' && c != '|') {
        out[used++] = (char)c;
      } else {
        out[used++] = '_';
      }
    }
  }
  out[used] = '\0';
  if (!used) {
    static unsigned sequence;
    snprintf(out, size, "Campicard_%lld_%u.png", (long long)time(NULL), sequence++);
  }
}

static int nx_gallery_save(const NxIl2CppByteArray *image, const void *album,
                           const NxIl2CppString *filename, const void *callback,
                           const void *method) {
  (void)album;
  (void)callback;
  (void)method;

  if (!image || image->length == 0 || image->length > 64u * 1024u * 1024u) {
    return 1;
  }

  char name[256];
  char path[512];
  nx_gallery_filename(filename, name, sizeof name);
  mkdir(GAME_HOME "/Campicards", 0777);
  if (snprintf(path, sizeof path, GAME_HOME "/Campicards/%s", name) >= (int)sizeof path) {
    return 1;
  }

  FILE *file = fopen(path, "wb");
  size_t written = file ? fwrite(image->data, 1, image->length, file) : 0;
  if (file) fclose(file);
  if (written != image->length) {
    if (file) remove(path);
  }
  return 1;
}

static int nx_install_gallery_hook(uintptr_t ib) {
  static const uint32_t expected[4] = {
    0xaa0303e4u, 0x52800023u, 0x14000001u, 0xf81c0ffeu,
  };
  const uint32_t *site = (const uint32_t *)(ib + ACPC_IL2CPP_GALLERY_SAVE);
  if (memcmp(site, expected, sizeof expected) != 0) return 0;

  uint32_t stub[4] = {
    0x58000050u,
    0xd61f0200u,
    (uint32_t)((uintptr_t)&nx_gallery_save & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_gallery_save >> 32),
  };
  return so_patch_code((void *)site, stub, sizeof stub) == 0;
}

static int nx_grant_gallery_permissions(uintptr_t ib) {
  static const struct {
    uint32_t offset;
    uint32_t expected[2];
  } sites[] = {
    { ACPC_IL2CPP_GALLERY_CHECK_PERMISSION,   { 0xf81c0ffeu, 0xa9015ff8u } },
    { ACPC_IL2CPP_GALLERY_REQUEST_PERMISSION, { 0xd10143ffu, 0xa90167feu } },
  };
  static const uint32_t granted[2] = { 0x52800020u, 0xd65f03c0u };

  for (size_t i = 0; i < sizeof sites / sizeof *sites; i++) {
    void *site = (void *)(ib + sites[i].offset);
    if (memcmp(site, sites[i].expected, sizeof sites[i].expected) != 0 ||
        so_patch_code(site, granted, sizeof granted) != 0)
      return 0;
  }
  return 1;
}

uintptr_t g_guide_capture_resume;
static void *g_guide_presenter;

void nx_capture_guide_presenter_c(const void *state_machine) {
  if (state_machine)
    g_guide_presenter = *(void *const *)((const uint8_t *)state_machine + 0x20);
}

extern void nx_capture_guide_presenter(void);

static int nx_install_guide_capture(uintptr_t ib) {
  static const uint32_t expected_veneer[4] = {
    0xa90157f6u, 0xa9024ff4u, 0xd00198b6u, 0xd0016d55u,
  };
  uint32_t *site = (uint32_t *)(ib + ACPC_GUIDE_SHOW_MOVE);
  uint32_t *veneer = (uint32_t *)(ib + ACPC_GUIDE_CAPTURE_VENEER);
  if (*site != 0xd10183ffu ||
      memcmp(veneer, expected_veneer, sizeof expected_veneer) != 0)
    return 0;

  intptr_t delta = (intptr_t)veneer - (intptr_t)site;
  if ((delta & 3) || delta < -(1ll << 27) || delta >= (1ll << 27)) return 0;
  const uint32_t stub[4] = {
    0x58000050u, 0xd61f0200u,
    (uint32_t)((uintptr_t)&nx_capture_guide_presenter & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_capture_guide_presenter >> 32),
  };
  const uint32_t branch =
      0x14000000u | ((uint32_t)(delta >> 2) & 0x03ffffffu);
  g_guide_capture_resume = (uintptr_t)site + sizeof(uint32_t);
  return so_patch_code(veneer, stub, sizeof stub) == 0 &&
         so_patch_code(site, &branch, sizeof branch) == 0;
}

static int nx_skip_camera_permission_wait(uintptr_t ib) {
  uint32_t *site = (uint32_t *)(ib + ACPC_CARD_CAMERA_WAIT);
  const uint32_t expected = 0x340001a8u;
  const uint32_t skip_wait = 0x1400000du;
  if (*site != expected) return 0;
  return so_patch_code(site, &skip_wait, sizeof skip_wait) == 0;
}

static void *nx_ar_close_entry(void *presenter, int call_type,
                               void *go_to_ar_event, const void *method) {
  (void)call_type;
  (void)go_to_ar_event;
  (void)method;
  void *on_close = presenter
      ? *(void **)((unsigned char *)presenter + 0x30) : NULL;
  if (on_close) {
    typedef void (*UnityEventInvokeFn)(void *, const void *);
    ((UnityEventInvokeFn)(g_il2cpp_base + ACPC_UNITY_EVENT_INVOKE))(
        on_close, NULL);
  }
  return NULL;
}

static int nx_disable_ar_entry(uintptr_t ib) {
  static const uint32_t expected[4] = {
    0xa9bd5ffeu, 0xa90157f6u, 0xa9024ff4u, 0x90012476u,
  };
  return nx_install_absolute_hook((uint32_t *)(ib + ACPC_AR_ENTRY_CONFIRM),
                                  expected, (uintptr_t)&nx_ar_close_entry);
}

static int nx_finish_dialog_loading(void *state_machine, const void *method) {
  (void)method;
  if (!state_machine) return 0;
  unsigned char *sm = state_machine;
  unsigned char *dialog = *(unsigned char **)(sm + 0x20);
  *(int32_t *)(sm + 0x10) = -1;
  if (!dialog) return 0;

  unsigned char *loading_icon = *(unsigned char **)(dialog + 0xb8);
  void *loading_object = *(void **)(dialog + 0xb0);
  if (loading_icon) {
    typedef void (*ForceStopFn)(void *, const void *);
    ((ForceStopFn)(g_il2cpp_base + ACPC_LOADING_ICON_FORCE_STOP))(loading_icon, NULL);
  } else if (loading_object) {
    typedef void (*SetActiveFn)(void *, int, const void *);
    ((SetActiveFn)(g_il2cpp_base + ACPC_GAMEOBJECT_SET_ACTIVE))(loading_object, 0, NULL);
  }

  const unsigned char *klass = *(const unsigned char **)dialog;
  if (klass) {
    typedef void (*ActivateContentsFn)(void *, int, const void *);
    ActivateContentsFn activate = *(ActivateContentsFn *)(klass + 632);
    const void *activate_method = *(const void **)(klass + 640);
    if (activate) activate(dialog, 1, activate_method);
  }
  int32_t *dialog_state = (int32_t *)(dialog + 0xec);
  if (*dialog_state == 3 || *dialog_state == 4) *dialog_state = 3;

  const unsigned char *presenter = g_guide_presenter;
  const void *guide_dialog = presenter ? *(void *const *)(presenter + 0x20) : NULL;
  if (dialog == guide_dialog) {
    void *animation = *(void **)(dialog + 0x110);
    void *show_clip = *(void **)(dialog + 0x78);
    if (animation && show_clip) {
      typedef void (*AnimationFn)(void *, void *, const void *);
      typedef void (*SampleFn)(void *, const void *);
      ((AnimationFn)(g_il2cpp_base + ACPC_ANIMATION_SKIP))(animation, show_clip, NULL);
      ((SampleFn)(g_il2cpp_base + ACPC_ANIMATION_SAMPLE))(animation, NULL);
    }
  }
  return 0;
}

static int nx_fix_dialog_loading_wait(uintptr_t ib) {
  static const uint32_t expected[4] = {
    0xa9be57feu, 0xa9014ff4u, 0xf0014354u, 0x394f3688u,
  };
  uint32_t *site = (uint32_t *)(ib + ACPC_DIALOG_END_LOADING);
  if (memcmp(site, expected, sizeof expected) != 0) return 0;
  uint32_t stub[4] = {
    0x58000050u, 0xd61f0200u,
    (uint32_t)((uintptr_t)&nx_finish_dialog_loading & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_finish_dialog_loading >> 32),
  };
  return so_patch_code(site, stub, sizeof stub) == 0;
}

static void *nx_find_engine_locale_class(void) {
  if (g_locale_class) return g_locale_class;
  if (!g_i_domain_get || !g_i_domain_get_assemblies || !g_i_assembly_get_image ||
      !g_i_class_from_name) return NULL;

  void *domain = g_i_domain_get();
  size_t count = 0;
  const void **assemblies = g_i_domain_get_assemblies(domain, &count);
  for (size_t i = 0; assemblies && i < count; i++) {
    const void *image = g_i_assembly_get_image(assemblies[i]);
    void *klass = image ? g_i_class_from_name(image, "NDcube.Engine", "Locale") : NULL;
    if (klass) {
      g_locale_class = klass;
      return klass;
    }
  }
  return NULL;
}

static void nx_set_locale_field(void *klass, const char *name, void *value) {
  void *field = g_i_class_get_field ? g_i_class_get_field(klass, name) : NULL;
  if (field) g_i_field_static_set(field, value);
}

static void nx_locale_initialize_hook(const void *method) {
  (void)method;
  void *klass = nx_find_engine_locale_class();
  const char *language = jni_locale_language();
  const char *country  = jni_locale_country();
  const char *locale   = jni_locale_name();
  if (!klass || !g_i_string_new || !g_i_field_static_set) return;

  void *s_country  = g_i_string_new(country);
  void *s_language = g_i_string_new(language);
  void *s_locale   = g_i_string_new(locale);
  if (!s_country || !s_language || !s_locale) return;
  nx_set_locale_field(klass, "_country",             s_country);
  nx_set_locale_field(klass, "_language",            s_locale);
  nx_set_locale_field(klass, "_app_country",         s_country);
  nx_set_locale_field(klass, "_app_language",        s_language);
  nx_set_locale_field(klass, "_app_default_country", NULL);
}

static int nx_install_locale_hook(uintptr_t ib) {
  static const uint32_t expected[4] = {
    0xf81d0ffeu, 0xa90157f6u, 0xa9024ff4u, 0x900197d4u,
  };
  const uint32_t *site = (const uint32_t *)(ib + ACPC_IL2CPP_LOCALE_INIT);
  if (memcmp(site, expected, sizeof expected) != 0) return 0;

  g_i_domain_get = (il2cpp_domain_get_fn)so_try_find_addr_rx(&il2cpp_mod, "il2cpp_domain_get");
  g_i_domain_get_assemblies = (il2cpp_domain_get_assemblies_fn)
      so_try_find_addr_rx(&il2cpp_mod, "il2cpp_domain_get_assemblies");
  g_i_assembly_get_image = (il2cpp_assembly_get_image_fn)
      so_try_find_addr_rx(&il2cpp_mod, "il2cpp_assembly_get_image");
  g_i_class_from_name = (il2cpp_class_from_name_fn)
      so_try_find_addr_rx(&il2cpp_mod, "il2cpp_class_from_name");
  g_i_class_get_field = (il2cpp_class_get_field_from_name_fn)
      so_try_find_addr_rx(&il2cpp_mod, "il2cpp_class_get_field_from_name");
  g_i_field_static_set = (il2cpp_field_static_set_value_fn)
      so_try_find_addr_rx(&il2cpp_mod, "il2cpp_field_static_set_value");
  g_i_string_new = (il2cpp_string_new_fn)
      so_try_find_addr_rx(&il2cpp_mod, "il2cpp_string_new");
  if (!g_i_domain_get || !g_i_domain_get_assemblies || !g_i_assembly_get_image ||
      !g_i_class_from_name || !g_i_class_get_field || !g_i_field_static_set ||
      !g_i_string_new) return 0;

  uint32_t stub[4] = {
    0x58000050u,
    0xd61f0200u,
    (uint32_t)((uintptr_t)&nx_locale_initialize_hook & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_locale_initialize_hook >> 32),
  };
  if (so_patch_code((void *)site, stub, sizeof stub) != 0) return 0;
  return 1;
}

static int nx_disable_local_notifications(uintptr_t ib) {
  struct VoidPatch { uint32_t off, expected; };
  static const struct VoidPatch void_patches[] = {
    { ACPC_LOCALNOTIFY_INIT,       0xf81d0ffeu },
    { ACPC_LOCALNOTIFY_PERMISSION, 0xf81e0ffeu },
    { ACPC_LOCALNOTIFY_REGISTER,   0xf81b0ffeu },
    { ACPC_LOCALNOTIFY_CANCEL,     0xd10103ffu },
  };
  for (size_t i = 0; i < sizeof void_patches / sizeof *void_patches; i++) {
    uint32_t have = *(const uint32_t *)(ib + void_patches[i].off);
    if (have != void_patches[i].expected) return 0;
  }
  if (*(const uint32_t *)(ib + ACPC_LOCALNOTIFY_IS_ENABLED) != 0xf81d0ffeu)
    return 0;

  const uint32_t ret = 0xd65f03c0u;
  for (size_t i = 0; i < sizeof void_patches / sizeof *void_patches; i++)
    if (so_patch_code((void *)(ib + void_patches[i].off), &ret, sizeof ret) != 0)
      return 0;

  const uint32_t return_false[2] = {
    0x2a1f03e0u,
    0xd65f03c0u,
  };
  if (so_patch_code((void *)(ib + ACPC_LOCALNOTIFY_IS_ENABLED),
                    return_false, sizeof return_false) != 0)
    return 0;
  return 1;
}

static int nx_force_local_asset_reachability(uintptr_t ib) {
  static const uint32_t expected[2] = {
    0xa9bf4ffeu,
    0xf0005b53u,
  };
  const uint32_t *site = (const uint32_t *)(ib + ACPC_IL2CPP_REACHABILITY);
  if (memcmp(site, expected, sizeof expected) != 0) return 0;

  static const uint32_t return_lan[2] = {
    0x52800040u,
    0xd65f03c0u,
  };
  if (so_patch_code((void *)site, return_lan, sizeof return_lan) != 0)
    return 0;
  return 1;
}

static int nx_use_local_device_time(uintptr_t ib) {
  static const uint32_t expected[2] = {
    0xf81a0ffeu,
    0xa9016ffcu,
  };
  const uint32_t *site = (const uint32_t *)(ib + ACPC_NTP_GET_NOW);
  if (memcmp(site, expected, sizeof expected) != 0) return 0;

  static const uint32_t return_transmit_time[2] = {
    0xaa0103e0u,
    0xd65f03c0u,
  };
  return so_patch_code((void *)site, return_transmit_time,
                       sizeof return_transmit_time) == 0;
}

static int nx_force_packaged_asset_revision(uintptr_t ib) {
  const uint32_t need_force_expected[2] = {
    0xf81e0ffeu,
    0xa9014ff4u,
  };
  const uint32_t expiry_move_expected[2] = {
    0xa9bd5ffeu,
    0xa90157f6u,
  };
  const uint32_t *need_force =
      (const uint32_t *)(ib + ACPC_ASSETREV_NEED_FORCE);
  const uint32_t *expiry_move =
      (const uint32_t *)(ib + ACPC_ASSETCHECK_EXPIRY_MOVE);
  const uint32_t *first_run =
      (const uint32_t *)(ib + ACPC_ASSETCHECK_FIRST_RUN_FLAG);
  const uint32_t *version_branch =
      (const uint32_t *)(ib + ACPC_ASSETCHECK_VERSION_BRANCH);
  const uint32_t *loader_exists_branch =
      (const uint32_t *)(ib + ACPC_ASSETLOADER_EXISTS_BRANCH);

  if (memcmp(need_force, need_force_expected, sizeof need_force_expected) != 0 ||
      memcmp(expiry_move, expiry_move_expected, sizeof expiry_move_expected) != 0 ||
      *first_run != 0x52800029u ||
      *version_branch != 0x36000300u ||
      *loader_exists_branch != 0x36001260u)
    return 0;

  const uint32_t return_false[2] = {
    0x2a1f03e0u,
    0xd65f03c0u,
  };
  const uint32_t first_run_current = 0x2a1f03e9u;
  const uint32_t version_current = 0xd503201fu;
  const uint32_t skip_uri_exists_reject = 0xd503201fu;

  if (so_patch_code((void *)need_force, return_false, sizeof return_false) != 0 ||
      so_patch_code((void *)expiry_move, return_false, sizeof return_false) != 0 ||
      so_patch_code((void *)first_run, &first_run_current,
                    sizeof first_run_current) != 0 ||
      so_patch_code((void *)version_branch, &version_current,
                    sizeof version_current) != 0 ||
      so_patch_code((void *)loader_exists_branch, &skip_uri_exists_reject,
                    sizeof skip_uri_exists_reject) != 0)
    return 0;

  return 1;
}

static float nx_www_timeout(const void *method) {
  (void)method;
  return 1800.0f;
}

static int nx_install_www_timeout(uintptr_t ib) {
  static const struct {
    uint32_t off;
    uint32_t expected[4];
  } sites[] = {
    { ACPC_DN_WWW_TIMEOUT,
      { 0xf81f0ffeu, 0x94001464u, 0xb4000080u, 0xbd404800u } },
    { ACPC_CD_WWW_TIMEOUT,
      { 0xf81f0ffeu, 0x97fffba0u, 0xb4000080u, 0xbd411800u } },
  };
  for (size_t i = 0; i < sizeof sites / sizeof *sites; i++) {
    const uint32_t *site = (const uint32_t *)(ib + sites[i].off);
    if (memcmp(site, sites[i].expected, sizeof sites[i].expected) != 0)
      return 0;
  }

  const uint32_t stub[4] = {
    0x58000050u,
    0xd61f0200u,
    (uint32_t)((uintptr_t)&nx_www_timeout & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_www_timeout >> 32),
  };
  for (size_t i = 0; i < sizeof sites / sizeof *sites; i++)
    if (so_patch_code((void *)(ib + sites[i].off), stub, sizeof stub) != 0)
      return 0;
  return 1;
}

static void (*g_unity_update_body)(void *, double) = NULL;
static uint64_t g_clk_base_ns = 0;
static void   *g_tm = NULL;
static Mutex   g_clock_lock;
static pthread_mutex_t **g_vsync_mutex = NULL;
static pthread_cond_t **g_vsync_condition = NULL;
static volatile uint64_t *g_vsync_frame_counter = NULL;
static volatile uint64_t g_last_main_tick_ns = 0;
#define CLOCK_STALL_NS 100000000ULL
static uint64_t nx_now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static void nx_clock_tick(void *tm) {
  uint64_t now = nx_now_ns();
  if (!g_clk_base_ns) g_clk_base_ns = now;
  double wall   = (double)(now - g_clk_base_ns) / 1e9;
  double sref   = *(volatile double *)((char *)tm + 0xe8);
  double newTime = sref + wall;
  if (g_unity_update_body) g_unity_update_body(tm, newTime);
}
static void nx_time_update_hook(void *tm) {
  g_tm = tm;
  g_last_main_tick_ns = nx_now_ns();
  *(volatile uint64_t *)((char *)tm + 0xc8) += 1;
  *(volatile uint32_t *)((char *)tm + 0xd0) += 1;
  if (*(volatile uint8_t *)((char *)tm + 0xf8) != 0) return;
  mutexLock(&g_clock_lock);
  nx_clock_tick(tm);
  mutexUnlock(&g_clock_lock);
}
static Thread g_clock_thr;
static volatile int g_clock_stop = 0;
static int g_clock_started = 0;
static void nx_clock_thread(void *arg) {
  (void)arg;
  static uint8_t clk_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  install_bionic_tls(clk_tls);
  while (!g_clock_stop) {
    svcSleepThread(16666667ULL);
    if (g_vsync_mutex && g_vsync_condition && g_vsync_frame_counter &&
        pthread_mutex_lock_fake(g_vsync_mutex) == 0) {
      ++*g_vsync_frame_counter;
      pthread_cond_broadcast_fake(g_vsync_condition);
      pthread_mutex_unlock_fake(g_vsync_mutex);
    }
    void *tm = g_tm;
    if (tm && g_unity_update_body &&
        (nx_now_ns() - g_last_main_tick_ns) > CLOCK_STALL_NS &&
        mutexTryLock(&g_clock_lock)) {
      nx_clock_tick(tm);
      mutexUnlock(&g_clock_lock);
    }
  }
}
static int nx_start_clock_thread(void) {
  Result rc = threadCreate(&g_clock_thr, nx_clock_thread, NULL, NULL,
                           0x8000, 0x2C, -2);
  if (R_FAILED(rc)) return 0;
  rc = threadStart(&g_clock_thr);
  if (R_FAILED(rc)) {
    threadClose(&g_clock_thr);
    return 0;
  }
  g_clock_started = 1;
  return 1;
}
static void nx_stop_clock_thread(void) {
  g_clock_stop = 1;
  if (g_clock_started) {
    threadWaitForExit(&g_clock_thr);
    threadClose(&g_clock_thr);
    g_clock_started = 0;
  }
  g_tm = NULL;
}
static int nx_install_time_fix(void) {
  uintptr_t ub = (uintptr_t)unity_mod.load_virtbase;
  g_vsync_mutex = (pthread_mutex_t **)(ub + 0x18524d0);
  g_vsync_condition = (pthread_cond_t **)(ub + 0x18524f8);
  g_vsync_frame_counter = (volatile uint64_t *)(ub + 0x1852528);
  g_unity_update_body = (void (*)(void *, double))(ub + OFF_TimeManager_Update_body);
  uint32_t stub[4] = {
    0x58000050u,
    0xd61f0200u,
    (uint32_t)((uintptr_t)&nx_time_update_hook & 0xffffffffu),
    (uint32_t)((uintptr_t)&nx_time_update_hook >> 32),
  };
  return so_patch_code((void *)(ub + OFF_TimeManager_Update_entry),
                       stub, sizeof stub) == 0;
}

#define TLS_RESERVE_THREADS 56

typedef struct {
  Thread thread;
  UEvent stop_event;
  volatile int ready;
  uintptr_t page;
  int started;
  int retained;
} TlsReserveThread;

static TlsReserveThread g_tls_reserve[TLS_RESERVE_THREADS];

static void nx_tls_reserve_thread(void *arg) {
  TlsReserveThread *slot = (TlsReserveThread *)arg;
  slot->page = (uintptr_t)armGetTls() & ~(uintptr_t)0xFFF;
  __atomic_store_n(&slot->ready, 1, __ATOMIC_RELEASE);
  waitSingle(waiterForUEvent(&slot->stop_event), UINT64_MAX);
}

static void nx_reserve_tls_capacity(void) {
  unsigned created = 0;
  for (; created < TLS_RESERVE_THREADS; created++) {
    TlsReserveThread *slot = &g_tls_reserve[created];
    ueventCreate(&slot->stop_event, false);
    Result rc = threadCreate(&slot->thread, nx_tls_reserve_thread, slot, NULL,
                             0x4000, 0x2C, -2);
    if (R_FAILED(rc)) break;
    rc = threadStart(&slot->thread);
    if (R_FAILED(rc)) {
      threadClose(&slot->thread);
      break;
    }
    slot->started = 1;
    while (!__atomic_load_n(&slot->ready, __ATOMIC_ACQUIRE))
      svcSleepThread(1000000ULL);
  }

  for (unsigned i = 0; i < created; i++) {
    int first = 1;
    for (unsigned j = 0; j < i; j++) {
      if (g_tls_reserve[j].retained &&
          g_tls_reserve[j].page == g_tls_reserve[i].page) {
        first = 0;
        break;
      }
    }
    if (first) {
      g_tls_reserve[i].retained = 1;
    } else {
      ueventSignal(&g_tls_reserve[i].stop_event);
    }
  }
  for (unsigned i = 0; i < created; i++) {
    if (g_tls_reserve[i].retained) continue;
    threadWaitForExit(&g_tls_reserve[i].thread);
    threadClose(&g_tls_reserve[i].thread);
    g_tls_reserve[i].started = 0;
  }
}

static void nx_release_tls_capacity(void) {
  for (unsigned i = 0; i < TLS_RESERVE_THREADS; i++) {
    if (g_tls_reserve[i].started && g_tls_reserve[i].retained)
      ueventSignal(&g_tls_reserve[i].stop_event);
  }
  for (unsigned i = 0; i < TLS_RESERVE_THREADS; i++) {
    if (!g_tls_reserve[i].started || !g_tls_reserve[i].retained) continue;
    threadWaitForExit(&g_tls_reserve[i].thread);
    threadClose(&g_tls_reserve[i].thread);
    g_tls_reserve[i].started = 0;
  }
}

#define SO_REGION_BYTES (192u * 1024 * 1024)

static void *oc_reserve_code_window(size_t want, size_t *out_size) {
  *out_size = 0;
  const size_t align = MMAP_ARENA_ALIGN;
  const size_t minimum = (size_t)1024 * 1024 * 1024;
  size_t size = want & ~(align - 1);

  while (size >= minimum) {
    if (size > SIZE_MAX - align) return NULL;
    virtmemLock();
    void *raw = virtmemFindCodeMemory(size + align, 0);
    uintptr_t aligned = raw ? ALIGN_MEM((uintptr_t)raw, align) : 0;
    VirtmemReservation *rv = aligned
      ? virtmemAddReservation((void *)aligned, size) : NULL;
    virtmemUnlock();
    if (rv) {
      *out_size = size;
      return (void *)aligned;
    }
    size = (size / 2) & ~(align - 1);
  }
  return NULL;
}

void __libnx_initheap(void) {
  void *addr;
  size_t size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  size_t so_zone = SO_REGION_BYTES;
  if (so_zone > size / 2)
    so_zone = size / 2;

  extern char *fake_heap_start;
  extern char *fake_heap_end;

  const size_t align = MMAP_ARENA_ALIGN;
  uintptr_t heap_start = (uintptr_t)addr;
  uintptr_t heap_end = heap_start + size;
  uintptr_t aligned_end = heap_end & ~(uintptr_t)(align - 1);
  size_t fixed_size = MMAP_FIXED_BYTES;
  size_t pool_size = 0;

  if (aligned_end > heap_start) {
    size_t aligned_size = aligned_end - heap_start;
    size_t reserved = so_zone + fixed_size + NEWLIB_HEAP_MIN;
    if (aligned_size > reserved)
      pool_size = (aligned_size - reserved) & ~(align - 1);
  }
  if (pool_size > OC_POOL_TARGET) pool_size = OC_POOL_TARGET;
  if (pool_size < (size_t)1024 * 1024 * 1024)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));

  uintptr_t pool_base = aligned_end - pool_size;
  uintptr_t fixed_base = pool_base - fixed_size;
  uintptr_t so_base = fixed_base - so_zone;

  fake_heap_start = (char *)heap_start;
  fake_heap_end = (char *)so_base;
  heap_so_base = (void *)so_base;
  heap_so_limit = so_zone;
  g_mmap_arena_base = (void *)fixed_base;
  g_mmap_arena_size = fixed_size;
  g_oc_pool_base = (void *)pool_base;
  g_oc_pool_size = pool_size;
}

static void check_syscalls(void) {
  if (!envIsSyscallHinted(0x77)) fatal_error("svcMapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x78)) fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x73)) fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if (!envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75))
    fatal_error("Writable code-patch aliases are unavailable.");
  if (!envIsSyscallHinted(0x32) || !envIsSyscallHinted(0x33))
    fatal_error("Managed GC requires svcSetThreadActivity/GetThreadContext3.");
  if (envGetOwnProcessHandle() == INVALID_HANDLE) fatal_error("Own process handle is unavailable.");
}

static void check_data(void) {
  const char *files[] = {
    LIB_MAIN, LIB_UNITY, LIB_IL2CPP, LIB_TONE,
    "assets/bin/Data/Managed/Metadata/global-metadata.dat",
    "assets/bin/Data/data.unity3d",
    "assets/Android/builtinassetbundleindex.unity3d",
  };
  char path[768];
  struct stat st;
  for (unsigned i = 0; i < sizeof(files)/sizeof(*files); i++) {
    snprintf(path, sizeof path, "%s/%s", DATA_ROOT, files[i]);
    if (stat(path, &st) < 0)
      fatal_error("Missing data file:\n%s\nCheck your SD card layout (see README.md).", files[i]);
  }
}

static int load_module(so_module *mod, const char *name) {
  char path[768];
  snprintf(path, sizeof path, "%s/%s", DATA_ROOT, name);
  if (so_load(mod, path, heap_so_base, heap_so_limit) < 0)
    return -1;
  size_t used = ALIGN_MEM(mod->load_size, 0x1000);
  heap_so_base = (char *)heap_so_base + used;
  heap_so_limit -= used;
  resolve_imports(mod);
  return 0;
}

static fn_initJni  Unity_initJni;
static fn_gfxstate Unity_nativeRecreateGfxState;
static fn_v        Unity_nativeSendSurfaceChanged;
static fn_z        Unity_nativeRender;
static fn_inject   Unity_nativeInjectEvent;
static fn_v        Unity_nativeResume;
static fn_vz       Unity_nativeFocusChanged;
static fn_z        Unity_nativeDone;
static fn_v        Unity_nativeApplicationUnload;
static void      (*Unity_nativeSetInputString)(void*,void*,void*);
static fn_vz       Unity_nativeSetKeyboardIsVisible;
static fn_v        Unity_nativeSoftInputClosed;
static fn_v        Unity_nativeSoftInputCanceled;

static void nx_pump_keyboard(void *env, void *thiz) {
  char text[1024];
  int cancelled = 0;
  if (!editbox_take_result(text, sizeof text, &cancelled)) return;
  Unity_nativeSetKeyboardIsVisible(env, thiz, 0);
  if (cancelled)
    Unity_nativeSoftInputCanceled(env, thiz);
  else
    Unity_nativeSetInputString(env, thiz, jni_make_string(text));
  Unity_nativeSoftInputClosed(env, thiz);
}

static int nx_patch_unity_regions(uintptr_t ub) {
  static const struct { uint32_t off, from, to; } P[] = {
    {0x720d3c, 0x12be0009, 0x12bf8009}, {0x720d44, 0x92648d36, 0x92669536},
    {0x7215dc, 0xd35cfc28, 0xd35afc28}, {0x7215e0, 0x52a20009, 0x52a08009},
    {0x722f70, 0x52a20009, 0x52a08009}, {0x725240, 0xd35cfd29, 0xd35afd29},
    {0x725244, 0x52a2000a, 0x52a0800a}, {0x7256ec, 0x12be000a, 0x12bf800a},
    {0x7256f4, 0x92648d36, 0x92669536}, {0x727700, 0xd35cdc33, 0xd35ad433},
    {0x727704, 0xd35cfd15, 0xd35afd15}, {0x727794, 0x52a20008, 0x52a08008},
    {0x727ad0, 0xd35cfc28, 0xd35afc28}, {0x727ae0, 0x92646c28, 0x92667428},
    {0x727ae8, 0xd35c9c2a, 0xd35a942a}, {0x727afc, 0xb25c6feb, 0xb25e77eb},
    {0x727b00, 0xd35cdc29, 0xd35ad429}, {0x727b04, 0xf2a2000b, 0xf2a0800b},
    {0x727b44, 0xcb0a7108, 0xcb0a6908}, {0x727b6c, 0xd35c9c29, 0xd35a9429},
    {0x7298b0, 0xd35c9e89, 0xd35a9689},
  };
  const int N = (int)(sizeof P / sizeof P[0]);
  for (int i = 0; i < N; i++) {
    uint32_t cur = *(volatile uint32_t *)(ub + P[i].off);
    if (cur != P[i].from) return 0;
  }
  for (int i = 0; i < N; i++)
    if (so_patch_code((void *)(ub + P[i].off), &P[i].to, sizeof P[i].to) != 0)
      return 0;
  return 1;
}

static int nx_patch_tone_counter(uintptr_t tb) {
  volatile uint32_t *site = (volatile uint32_t *)(tb + ACPC_TONE_TICK_SITE);
  uint32_t cur = *site;
  if (cur != ACPC_TONE_TICK_FROM) return 0;

  uint32_t physical_counter = ACPC_TONE_TICK_TO;
  if (so_patch_code((void *)site, &physical_counter, sizeof physical_counter) != 0)
    return 0;
  return 1;
}

static int nx_disable_android_frame_time_tracker(uintptr_t ub) {
  static const uint32_t sites[] = {
    ACPC_FTT_ENABLE_SITE,
    ACPC_FTT_DISABLE_SITE,
  };
  const int count = (int)(sizeof sites / sizeof sites[0]);

  for (int i = 0; i < count; i++) {
    uint32_t cur = *(volatile uint32_t *)(ub + sites[i]);
    if (cur != ACPC_FTT_ENTRY_FROM) return 0;
  }
  for (int i = 0; i < count; i++) {
    uint32_t ret = ACPC_FTT_ENTRY_TO;
    if (so_patch_code((void *)(ub + sites[i]), &ret, sizeof ret) != 0)
      return 0;
  }
  return 1;
}

typedef void (*UnitySetTargetFrameRateFn)(int);
static UnitySetTargetFrameRateFn g_unity_set_target_frame_rate;

static void nx_set_target_frame_rate(int value, const void *method) {
  (void)value;
  (void)method;
  g_unity_set_target_frame_rate(60);
}

static int nx_force_target_frame_rate(uintptr_t ib, uintptr_t ub) {
  const uint32_t *unity_site =
      (const uint32_t *)(ub + ACPC_UNITY_TARGET_FRAME_RATE);
  static const uint32_t unity_expected[3] = {
    0xf0007da8u, 0xb90e6900u, 0x1406abc7u,
  };
  static const uint32_t il2cpp_expected[4] = {
    0xf81e0ffeu, 0xa9014ff4u, 0xd0005b54u, 0xf947f681u,
  };
  if (memcmp(unity_site, unity_expected, sizeof unity_expected) != 0)
    return 0;

  g_unity_set_target_frame_rate =
      (UnitySetTargetFrameRateFn)(ub + ACPC_UNITY_TARGET_FRAME_RATE);
  return nx_install_absolute_hook(
      (uint32_t *)(ib + ACPC_IL2CPP_SET_TARGET_FRAME_RATE),
      il2cpp_expected, (uintptr_t)&nx_set_target_frame_rate);
}

typedef struct NxUnityTlsErrorState {
  uint32_t magic;
  uint32_t code;
  uint64_t user_data;
} NxUnityTlsErrorState;

static int nx_install_unity_ca_bundle(uintptr_t ub) {
  static const struct {
    uint32_t off;
    uint32_t word0;
    uint32_t word1;
  } api[] = {
    { ACPC_UNITYTLS_ERRORSTATE_CREATE, 0x529f58e0u, 0x72a0d960u },
    { ACPC_UNITYTLS_CA_ENTER,          0xf81e0ffeu, 0xa9014ff4u },
    { ACPC_UNITYTLS_CA_EXIT,           0xa9be57feu, 0xa9014ff4u },
    { ACPC_UNITYTLS_CA_APPEND_PEM,     0xa9ba7bfdu, 0xa9016ffcu },
  };
  for (size_t i = 0; i < sizeof api / sizeof api[0]; i++) {
    const uint32_t *site = (const uint32_t *)(ub + api[i].off);
    if (site[0] != api[i].word0 || site[1] != api[i].word1) return 0;
  }

  typedef NxUnityTlsErrorState (*ErrorCreateFn)(void);
  typedef void *(*CaEnterFn)(NxUnityTlsErrorState *error);
  typedef void (*CaExitFn)(void *list, NxUnityTlsErrorState *error);
  typedef int (*AppendPemFn)(void *list, const uint8_t *pem, size_t len,
                             NxUnityTlsErrorState *error);
  ErrorCreateFn create_error =
      (ErrorCreateFn)(ub + ACPC_UNITYTLS_ERRORSTATE_CREATE);
  CaEnterFn enter_ca = (CaEnterFn)(ub + ACPC_UNITYTLS_CA_ENTER);
  CaExitFn exit_ca = (CaExitFn)(ub + ACPC_UNITYTLS_CA_EXIT);
  AppendPemFn append_pem = (AppendPemFn)(ub + ACPC_UNITYTLS_CA_APPEND_PEM);

  NxUnityTlsErrorState error = create_error();
  void *ca_list = enter_ca(&error);
  if (!ca_list || error.code != 0) {
    if (ca_list) exit_ca(ca_list, &error);
    return 0;
  }
  int appended = append_pem(ca_list, cacerts_pem, cacerts_pem_size, &error);
  exit_ca(ca_list, &error);
  if (!appended || error.code != 0) return 0;
  return 1;
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;

  SocketInitConfig net_config = *socketGetDefaultInitConfig();
  net_config.num_bsd_sessions = 8;
  Result net_rc = socketInitialize(&net_config);
  if (R_FAILED(net_rc))
    net_rc = socketInitializeDefault();
  if (R_FAILED(net_rc))
    fatal_error("Could not initialize networking (0x%x)", net_rc);

  if (chdir(DATA_ROOT) != 0) fatal_error("Could not enter %s", DATA_ROOT);

  {
    const char *cfg = DATA_ROOT "/" CONFIG_NAME;
    int rc = read_config(cfg);
    if (rc != 0) write_config(cfg);
    if (config.portrait != 2) config.portrait = 1;
  }
  check_syscalls();

  {
    size_t winsz = 0;
    void *win = oc_reserve_code_window(OC_WINDOW_BYTES, &winsz);
    if (!win || !winsz)
      fatal_error("Could not reserve Unity virtual address space");

    if (!g_oc_pool_base || !g_oc_pool_size)
      fatal_error("Unity memory backing is unavailable");
    if (!oc_arena_init(win, winsz, g_oc_pool_base, g_oc_pool_size)) {
      fatal_error("Could not initialize Unity virtual memory");
    }
  }

  nx_reserve_tls_capacity();

  if (appletGetOperationMode() == AppletOperationMode_Console) {
    screen_width = ACPC_DOCKED_WIDTH;
    screen_height = ACPC_DOCKED_HEIGHT;
  } else {
    screen_width = ACPC_HANDHELD_WIDTH;
    screen_height = ACPC_HANDHELD_HEIGHT;
  }

  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    fatal_error("SDL initialization failed: %s", SDL_GetError());

  check_data();

  if (load_module(&main_mod,   LIB_MAIN)   < 0) fatal_error("Could not load %s", LIB_MAIN);
  if (load_module(&unity_mod,  LIB_UNITY)  < 0) fatal_error("Could not load %s", LIB_UNITY);
  if (load_module(&il2cpp_mod, LIB_IL2CPP) < 0) fatal_error("Could not load %s", LIB_IL2CPP);
  if (load_module(&tone_mod,   LIB_TONE)   < 0) fatal_error("Could not load %s", LIB_TONE);
  g_il2cpp_base = (uintptr_t)il2cpp_mod.load_virtbase;

  so_finalize(&main_mod);   so_flush_caches(&main_mod);
  so_finalize(&unity_mod);  so_flush_caches(&unity_mod);
  so_finalize(&il2cpp_mod); so_flush_caches(&il2cpp_mod);
  so_finalize(&tone_mod);   so_flush_caches(&tone_mod);
  if (!nx_install_locale_hook((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install native game Locale initializer");
  if (!nx_install_gallery_hook((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install Campicard save hook");
  if (!nx_grant_gallery_permissions((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not bypass Android gallery permissions");
  if (!nx_disable_local_notifications((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not disable Android local notifications");
  if (!nx_install_guide_capture((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install Personal Guide compatibility hook");
  if (!nx_fix_dialog_loading_wait((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install dialog loading fix");
  if (!nx_skip_camera_permission_wait((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not bypass camera permission wait");
  if (!nx_disable_ar_entry((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not disable unsupported AR mode");
  if (!nx_force_local_asset_reachability((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install packaged-asset reachability override");
  if (!nx_use_local_device_time((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install offline time fallback");
  if (!nx_force_packaged_asset_revision((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install offline packaged-asset revision override");
  if (!nx_install_www_timeout((uintptr_t)il2cpp_mod.load_virtbase))
    fatal_error("Could not install download timeout patch");

  {
    uintptr_t ub = (uintptr_t)unity_mod.load_virtbase;
    volatile uint32_t *site = (volatile uint32_t *)(ub + ACPC_FMOD_SETOUTPUT_SITE);
    uint32_t opensl = ACPC_FMOD_SETOUTPUT_TO;
    if (*site != ACPC_FMOD_SETOUTPUT_FROM ||
        so_patch_code((void *)site, &opensl, sizeof opensl) != 0)
      fatal_error("Could not select FMOD OpenSL output");
  }

  if (!nx_patch_unity_regions((uintptr_t)unity_mod.load_virtbase))
    fatal_error("Could not patch Unity allocator regions");

  if (!nx_disable_android_frame_time_tracker((uintptr_t)unity_mod.load_virtbase))
    fatal_error("Could not disable Unity's Android FrameTimeTracker");
  if (!nx_force_target_frame_rate((uintptr_t)il2cpp_mod.load_virtbase,
                                  (uintptr_t)unity_mod.load_virtbase))
    fatal_error("Could not force Unity target frame rate");

  if (!nx_patch_tone_counter((uintptr_t)tone_mod.load_virtbase))
    fatal_error("Could not install the libTone Horizon counter patch");

  static uint8_t main_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  install_bionic_tls(main_tls);

  so_execute_init_array(&main_mod);
  so_execute_init_array(&unity_mod);
  so_execute_init_array(&il2cpp_mod);
  so_execute_init_array(&tone_mod);
  if (!nx_install_unity_ca_bundle((uintptr_t)unity_mod.load_virtbase))
    fatal_error("Could not install UnityTLS CA root bundle");
  so_free_temp(&main_mod); so_free_temp(&unity_mod); so_free_temp(&il2cpp_mod); so_free_temp(&tone_mod);

  jni_init();
  unity_environment_init(DATA_ROOT);
  android_native_update_mode();
  android_native_input_init();

  Unity_initJni                  = (fn_initJni) UNITY_RESOLVE(unity_mod, OFF_initJni);
  Unity_nativeRecreateGfxState   = (fn_gfxstate)UNITY_RESOLVE(unity_mod, OFF_nativeRecreateGfxState);
  Unity_nativeSendSurfaceChanged = (fn_v)       UNITY_RESOLVE(unity_mod, OFF_nativeSendSurfaceChangedEvent);
  Unity_nativeRender             = (fn_z)       UNITY_RESOLVE(unity_mod, OFF_nativeRender);
  Unity_nativeInjectEvent        = (fn_inject)  UNITY_RESOLVE(unity_mod, OFF_nativeInjectEvent);
  Unity_nativeResume             = (fn_v)       UNITY_RESOLVE(unity_mod, OFF_nativeResume);
  Unity_nativeFocusChanged       = (fn_vz)      UNITY_RESOLVE(unity_mod, OFF_nativeFocusChanged);
  Unity_nativeDone               = (fn_z)       UNITY_RESOLVE(unity_mod, OFF_nativeDone);
  Unity_nativeApplicationUnload  = (fn_v)       UNITY_RESOLVE(unity_mod, OFF_nativeApplicationUnload);
  Unity_nativeSetInputString      = (void(*)(void*,void*,void*))UNITY_RESOLVE(unity_mod, OFF_nativeSetInputString);
  Unity_nativeSetKeyboardIsVisible = (fn_vz)    UNITY_RESOLVE(unity_mod, OFF_nativeSetKeyboardIsVisible);
  Unity_nativeSoftInputClosed     = (fn_v)       UNITY_RESOLVE(unity_mod, OFF_nativeSoftInputClosed);
  Unity_nativeSoftInputCanceled   = (fn_v)       UNITY_RESOLVE(unity_mod, OFF_nativeSoftInputCanceled);

  install_bionic_tls(main_tls);

  extern void *fake_env, *fake_unityplayer_thiz, *fake_context_obj, *fake_surface_obj;
  extern void *fake_vm;

  {
    typedef int (*fn_jnionload)(void *vm, void *reserved);
    fn_jnionload Unity_JNI_OnLoad = (fn_jnionload)UNITY_RESOLVE(unity_mod, OFF_JNI_OnLoad);
    Unity_JNI_OnLoad(fake_vm, NULL);
  }

  {
    uintptr_t b = (uintptr_t)il2cpp_mod.load_virtbase;
    *(void **)(b + 0x61995e8) = fake_vm;
    *(void **)(b + 0x6198580) = (void *)(b + 0x288ade8);
  }

  Unity_initJni(fake_env, fake_unityplayer_thiz, fake_context_obj);
  Unity_nativeRecreateGfxState(fake_env, fake_unityplayer_thiz, 0, fake_surface_obj);
  Unity_nativeSendSurfaceChanged(fake_env, fake_unityplayer_thiz);

  Unity_nativeResume(fake_env, fake_unityplayer_thiz);
  Unity_nativeFocusChanged(fake_env, fake_unityplayer_thiz, 1);

  {
    typedef void (*fn_set_mode)(int);
    fn_set_mode il2cpp_gc_set_mode = (fn_set_mode)so_try_find_addr_rx(&il2cpp_mod, "il2cpp_gc_set_mode");
    if (il2cpp_gc_set_mode) il2cpp_gc_set_mode(1);
    if (!nx_install_time_fix()) fatal_error("Could not install Unity frame clock");
  }
  if (!nx_start_clock_thread()) fatal_error("Could not start Unity frame clock");

  for (;;) {
    if (!appletMainLoop()) break;
    android_native_update_mode();
    android_native_feed_hid((uint8_t (*)(void*,void*,void*,int))Unity_nativeInjectEvent,
                            fake_env, fake_unityplayer_thiz);
    nx_pump_keyboard(fake_env, fake_unityplayer_thiz);
    int keep_running = Unity_nativeRender(fake_env, fake_unityplayer_thiz);
    if (!keep_running) break;
  }

  nx_stop_clock_thread();
  Unity_nativeApplicationUnload(fake_env, fake_unityplayer_thiz);
  Unity_nativeDone(fake_env, fake_unityplayer_thiz);

  opensles_shutdown();
  SDL_Quit();
  nx_release_tls_capacity();
  socketExit();

  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(0);
  return 0;
}
