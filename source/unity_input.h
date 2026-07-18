#ifndef UNITY_INPUT_H
#define UNITY_INPUT_H

#include <stdarg.h>
#include <stdint.h>

#define UI_MAX_POINTERS 10

#define AMOTION_ACTION_DOWN          0
#define AMOTION_ACTION_UP            1
#define AMOTION_ACTION_MOVE          2
#define AMOTION_ACTION_CANCEL        3
#define AMOTION_ACTION_POINTER_DOWN  5
#define AMOTION_ACTION_POINTER_UP    6
#define AMOTION_ACTION_MASK          0xff
#define AMOTION_ACTION_PTR_IDX_SHIFT 8
#define AINPUT_SOURCE_TOUCHSCREEN    0x1002
#define AINPUT_SOURCE_KEYBOARD       0x0101
#define AMOTION_TOOL_TYPE_FINGER     1
#define AKEY_ACTION_DOWN             0
#define AKEY_ACTION_UP               1
#define AKEYCODE_BACK                4

void *unity_motionevent(int action, int count,
                        const int *ids, const float *xs, const float *ys);
void *unity_keyevent(int action, int keycode);
void *unity_motionevent_obtain(void *src);

int input_owns_class(const char *cls);
int input_owns_recv(const void *recv);
int input_recv_is_motion(const void *recv);
uint64_t input_dispatch_int(void *recv, const void *id, va_list va);
float input_dispatch_float(void *recv, const void *id, va_list va);

#endif
