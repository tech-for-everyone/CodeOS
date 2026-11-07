#ifndef DRIVERS_H
#define DRIVERS_H

#include "types.h"

/* Driver category classification */
typedef enum {
    DRIVER_CLASS_SYSTEM = 0,
    DRIVER_CLASS_TIMER,
    DRIVER_CLASS_BUS,
    DRIVER_CLASS_INPUT,
    DRIVER_CLASS_MOUSE,
    DRIVER_CLASS_KEYBOARD,
    DRIVER_CLASS_STORAGE,
    DRIVER_CLASS_NETWORK,
    DRIVER_CLASS_AUDIO,
    DRIVER_CLASS_VIDEO,
    DRIVER_CLASS_PERIPHERAL
} driver_class_t;

/* Driver load status */
typedef enum {
    DRIVER_STATUS_UNLOADED = 0,
    DRIVER_STATUS_DETECTED,
    DRIVER_STATUS_LOADED,
    DRIVER_STATUS_NOT_PRESENT,
    DRIVER_STATUS_FAILED
} driver_status_t;

/* Driver module descriptor for automated runtime detection & dynamic RAM loading */
typedef struct driver_module {
    const char      *name;
    const char      *description;
    driver_class_t   category;
    int            (*probe)(void);                 /* Hardware detection probe (returns 1 if detected) */
    int            (*load)(void *ram_instance);    /* Dynamically initializes driver in volatile RAM */
    void           (*unload)(void *ram_instance);  /* Unloads driver and tears down resources */
    uint32_t         ram_size;                     /* Size of driver state block allocated in RAM */
    void            *ram_instance;                 /* Volatile memory address (heap/RAM) */
    driver_status_t  status;                       /* Current lifecycle status */
    uint64_t         load_time_ms;                 /* Milliseconds from boot when loaded */
} driver_module_t;

/* Automated Driver Detection Subsystem Core API */
void drivers_init(void);
void drivers_detect_and_load(void);
void drivers_print_status(void);
void drivers_print_status_panel(void);
void drivers_dump_detection_table(void);

/* Dynamic Driver Inspection Queries */
int              driver_module_count(void);
driver_module_t *driver_get_module_by_idx(int idx);
driver_module_t *driver_find_module(const char *name);
int              driver_is_loaded(const char *name);
void            *driver_get_ram_address(const char *name);

#endif /* DRIVERS_H */
