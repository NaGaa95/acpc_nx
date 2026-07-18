/* error.c -- error handler
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <switch.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "error.h"

void fatal_error(const char *fmt, ...) {
  char message[2048];
  va_list list;
  va_start(list, fmt);
  vsnprintf(message, sizeof message, fmt, list);
  va_end(list);

  ErrorApplicationConfig config;
  Result rc = errorApplicationCreate(&config, message, message);
  if (R_SUCCEEDED(rc)) {
    errorApplicationSetNumber(&config, 1);
    rc = errorApplicationShow(&config);
  }
  if (R_FAILED(rc))
    fatalThrow(MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen));
  exit(EXIT_FAILURE);
}
