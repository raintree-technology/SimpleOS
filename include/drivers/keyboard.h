#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>

void init_keyboard(void);

// Check if keyboard has character available
bool keyboard_has_char(void);

// Get character from keyboard buffer (returns 0 if none available)
char keyboard_getchar(void);

#endif // KEYBOARD_H