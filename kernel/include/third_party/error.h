#ifndef __ERROR_H
#define __ERROR_H

extern void handle_load_access_error(void);
extern void handle_store_access_error(void);
extern void handle_clock_error(void);
extern void handle_umode_error(void);
extern void handle_load_page_error(void);
extern void handle_store_page_error(void);
extern void handle_instruction_page_error(void);
extern void handle_breakpoint(void);

#endif
