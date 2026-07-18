/* Fake JNI environment for Pocket Camp's Android Unity player.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

#include <stdint.h>

extern void *fake_vm;
extern void *fake_env;

void jni_init(void);

void *jni_make_string(const char *utf);
void *jni_make_object(const char *label);

const char *jni_locale_language(void);
const char *jni_locale_country(void);
const char *jni_locale_name(void);

#endif
