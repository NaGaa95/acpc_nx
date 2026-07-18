#ifndef __EDITBOX_H__
#define __EDITBOX_H__

void editbox_show(const char *initial, int maxlen);
int editbox_is_open(void);
const char *editbox_text(void);
void editbox_close(void);
int editbox_take_result(char *out, int out_size, int *cancelled);

#endif
