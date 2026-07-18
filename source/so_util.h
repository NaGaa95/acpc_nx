/* so_util.h -- utils to load and hook .so modules
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#include <stdint.h>
#include <stddef.h>
#include <elf.h>

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#define SO_MAX_SEGMENTS 8

typedef struct {
  char *symbol;
  uintptr_t func;
} DynLibFunction;

typedef struct so_module {
  struct so_module *next;
  char name[64];

  /* LOAD zone. */
  void *load_base, *load_virtbase;
  size_t load_size;
  void *load_memrv; // VirtmemReservation *

  /* Link-time program headers. */
  Elf64_Phdr phdr[SO_MAX_SEGMENTS * 2];
  int phnum;

  /* Temporary file image. */
  void *so_base;
  size_t so_size;

  Elf64_Ehdr *elf_hdr;
  Elf64_Phdr *prog_hdr;
  Elf64_Shdr *sec_hdr;
  Elf64_Sym *syms;
  int num_syms;
  char *shstrtab;
  char *dynstrtab;
} so_module;

void so_flush_caches(so_module *mod);
void so_free_temp(so_module *mod);
int so_load(so_module *mod, const char *filename, void *base, size_t max_size);
int so_relocate(so_module *mod);
int so_resolve(so_module *mod, DynLibFunction *funcs, int num_funcs, int taint_missing_imports);
void so_execute_init_array(so_module *mod);
uintptr_t so_try_find_addr_rx(so_module *mod, const char *symbol);
/* Search loaded modules for dlsym(). */
void *so_resolve_external(const char *name);
int so_dump_maps(char *buf, size_t cap);
void so_finalize(so_module *mod);

/* dl_iterate_phdr replacement for loaded modules. */
int so_dl_iterate_phdr(int (*callback)(void *info, size_t size, void *data), void *data);

int so_patch_code(void *dst, const void *src, size_t len);

#endif
