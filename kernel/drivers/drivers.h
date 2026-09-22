#ifndef DRIVERS_H
#define DRIVERS_H

/* Core hardware drivers (no Linux kernel code — CodeOS drivers only). */
void drivers_init(void);
void drivers_print_status(void);
void drivers_print_status_panel(void);

#endif
