#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdint.h>

#define DL_MAX_JOBS 8
#define DL_URL_MAX  384
#define DL_FILE_MAX 64

typedef enum { DL_IDLE, DL_DOWNLOADING, DL_COMPLETE, DL_ERROR } dl_state_t;

typedef struct {
    char url[DL_URL_MAX];
    char filename[DL_FILE_MAX];
    dl_state_t state;
    int  progress;
    int  size;        
    int  data_len;    
    char *data;       
} dl_job_t;

void dl_init(void);
int  dl_open(void);
int  dl_is_open(void);
void dl_draw(void);
int  dl_click(int mx, int my);
int  dl_key(int key);
void dl_close(void);
void dl_update(void);
int  dl_start(const char *url, const char *filename);

#endif
