/* Animal Crossing: Pocket Camp Complete Switch configuration. */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define MMAP_ARENA_ALIGN    ((size_t)64 * 1024 * 1024)
#define MMAP_FIXED_BYTES    ((size_t) 192 * 1024 * 1024)
#define OC_WINDOW_BYTES     ((size_t)8192 * 1024 * 1024)
#define OC_POOL_TARGET      ((size_t)1792 * 1024 * 1024)
#define NEWLIB_HEAP_MIN     ((size_t)1024 * 1024 * 1024)

#define ACPC_PACKAGE       "com.nintendo.zasa"
#define ACPC_VERSION_CODE  51472
#define ACPC_VERSION_NAME  "7.1.3"

#define CONFIG_NAME "config.txt"
#define GAME_HOME   "sdmc:/switch/acpc_nx"

#define ACPC_HANDHELD_WIDTH   720
#define ACPC_HANDHELD_HEIGHT 1280
#define ACPC_DOCKED_WIDTH    1080
#define ACPC_DOCKED_HEIGHT   1920

extern int screen_width;
extern int screen_height;

#define LANG_AUTO 0
#define LANG_JA   1
#define LANG_EN   2

typedef struct {
  int language;
  int portrait;
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
