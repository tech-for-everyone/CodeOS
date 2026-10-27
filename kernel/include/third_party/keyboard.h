#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_ADDRESS SCREEN_ADDRESS
#define BUFFER_SIZE 64

char get_char(void);
void print_input(void);
char *get_input(void);
bool test_keyboard(void);

#endif
