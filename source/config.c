/* config.c -- simple configuration parser
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "util.h"

Config config;
static int config_needs_rewrite = 0;
static unsigned config_seen = 0;

int screen_width = ACPC_HANDHELD_WIDTH;
int screen_height = ACPC_HANDHELD_HEIGHT;

static inline void parse_var(const char *name, const char *value) {
  if (!strcmp(name, "language")) {
    config.language = atoi(value); config_seen |= 1; return;
  }
  if (!strcmp(name, "portrait")) {
    config.portrait = atoi(value); config_seen |= 2; return;
  }
  config_needs_rewrite = 1;
}

int read_config(const char *file) {
  char line[1024] = { 0 };

  memset(&config, 0, sizeof(Config));
  config_needs_rewrite = 0;
  config_seen = 0;
  config.language = LANG_AUTO;
  config.portrait = 1;

  FILE *f = fopen(file, "r");
  if (f == NULL)
    return -1;

  do {
    char *name = NULL, *value = NULL, *tmp = NULL;
    if (fgets(line, sizeof(line), f) != NULL) {
      name = line;
      while (*name && isspace((int)*name)) ++name;
      if (name[0] == '#') continue;
      for (tmp = name; *tmp && !isspace((int)*tmp); ++tmp);
      if (*tmp != 0) {
        *tmp = 0;
        for (value = tmp + 1; *value && isspace((int)*value); ++value);
        for (tmp = value + strlen(value) - 1; isspace((int)*tmp); --tmp) *tmp = 0;
        parse_var(name, value);
      }
    }
  } while (!feof(f));

  fclose(f);
  if (config_seen != 3) config_needs_rewrite = 1;

  return config_needs_rewrite ? 1 : 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  fprintf(f, "language %d\n", config.language);
  fprintf(f, "portrait %d\n", config.portrait);

  fclose(f);

  return 0;
}
