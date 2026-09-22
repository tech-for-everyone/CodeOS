#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_putchar(char c);
int  serial_available(void);
char serial_readchar(void);
void serial_write(const char *s);

#endif
