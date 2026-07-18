#ifndef UNITY_JNI_H
#define UNITY_JNI_H

#include <stdarg.h>
#include <stdint.h>

void unity_jni_init(const char *data_root);
int unity_owns_class(const char *cls);

void *unity_dispatch_object(void *recv, const void *id, va_list va);
uint64_t unity_dispatch_int(void *recv, const void *id, va_list va);
void unity_dispatch_void(void *recv, const void *id, va_list va);

int unity_is_boxed(void *recv);
uint64_t unity_boxed_int(void *recv);
float unity_boxed_float(void *recv);
int unity_isinstance(void *obj, const char *clazz);

void *jni_make_string(const char *utf);
void *jni_make_object(const char *label);
void *jni_bytearray_data(void *arr, int *len_out);
const char *jni_string_utf(void *jstr);

#endif
