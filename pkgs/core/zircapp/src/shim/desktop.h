/* desktop.h shim for the CodeOS userspace ZircApp build.
 *
 * Zircon's gui/windows.h and gui/widgets.h both do `#include "desktop.h"`,
 * which in the Zircon build resolves to kernel/kernel/desktop.h (kernel GUI
 * types).  The CodeOS host only needs the `gui_window_*` data types from
 * gui/windows.h, which itself only needs stdint.h, so this shim is empty
 * apart from its guard and is placed early on the include path. */
#ifndef DESKTOP_H
#define DESKTOP_H
#endif
