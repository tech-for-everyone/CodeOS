#ifndef PS2_H
#define PS2_H

#include "types.h"

void ps2_init(void);
bool ps2_wait_output_ready(uint32_t timeout);
bool ps2_wait_input_ready(uint32_t timeout);
bool ps2_send_command(uint16_t port, uint8_t command);
bool ps2_send_data(uint8_t data);
int  ps2_read_data_ex(uint8_t *out, uint32_t timeout);
uint8_t ps2_read_data(void);
void ps2_flush(void);

#endif
