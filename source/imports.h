/* imports.h -- .so import resolution
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __IMPORTS_H__
#define __IMPORTS_H__

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "so_util.h"

extern FILE *stderr_fake;
extern DynLibFunction dynlib_functions[];
uintptr_t dynlib_find_export(const char *name);
extern size_t dynlib_numfunctions;

void abort_fake(void) __attribute__((noreturn));
void exit_fake(int code) __attribute__((noreturn));
int raise_fake(int sig);
int pthread_mutex_lock_fake(pthread_mutex_t **mutex);
int pthread_mutex_unlock_fake(pthread_mutex_t **mutex);
int pthread_cond_broadcast_fake(pthread_cond_t **condition);

/* Relocate a module against the loaded modules and shim table. */
void resolve_imports(so_module *mod);

#endif
