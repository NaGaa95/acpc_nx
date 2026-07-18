/* editbox.c -- libnx software-keyboard bridge for Unity TouchScreenKeyboard. */

#include <switch.h>
#include <stdio.h>
#include <string.h>

#include "editbox.h"

#define EDITBOX_TEXT_CAP 1024

static char g_editbox_text[EDITBOX_TEXT_CAP];
static int g_editbox_open;
static int g_editbox_pending;
static int g_editbox_cancelled;

void editbox_show(const char *initial, int maxlen) {
  if (g_editbox_open) return;
  g_editbox_open = 1;
  g_editbox_pending = 0;
  g_editbox_cancelled = 1;

  if (!initial) initial = "";
  snprintf(g_editbox_text, sizeof g_editbox_text, "%s", initial);
  if (maxlen <= 0 || maxlen >= EDITBOX_TEXT_CAP) maxlen = EDITBOX_TEXT_CAP - 1;

  SwkbdConfig keyboard;
  Result rc = swkbdCreate(&keyboard, 0);
  if (R_SUCCEEDED(rc)) {
    char result[EDITBOX_TEXT_CAP];
    snprintf(result, sizeof result, "%s", g_editbox_text);
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetInitialText(&keyboard, g_editbox_text);
    swkbdConfigSetStringLenMax(&keyboard, (u32)maxlen);
    rc = swkbdShow(&keyboard, result, sizeof result);
    if (R_SUCCEEDED(rc)) {
      snprintf(g_editbox_text, sizeof g_editbox_text, "%s", result);
      g_editbox_cancelled = 0;
    }
    swkbdClose(&keyboard);
  }

  g_editbox_open = 0;
  g_editbox_pending = 1;
}

int editbox_is_open(void) { return g_editbox_open; }
const char *editbox_text(void) { return g_editbox_text; }

void editbox_close(void) {
  if (g_editbox_open) g_editbox_cancelled = 1;
}

int editbox_take_result(char *out, int out_size, int *cancelled) {
  if (!g_editbox_pending) return 0;
  g_editbox_pending = 0;
  if (out && out_size > 0) snprintf(out, (size_t)out_size, "%s", g_editbox_text);
  if (cancelled) *cancelled = g_editbox_cancelled;
  return 1;
}
