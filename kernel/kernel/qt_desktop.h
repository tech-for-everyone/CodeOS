#ifndef QT_DESKTOP_H
#define QT_DESKTOP_H

#ifdef __cplusplus
extern "C" {
#endif

int  qt_desktop_init(void);
int  qt_desktop_active(void);
void qt_desktop_run(void);
void qt_desktop_stop(void);

#ifdef __cplusplus
}
#endif

#endif
