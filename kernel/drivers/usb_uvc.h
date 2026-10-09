#ifndef USB_UVC_H
#define USB_UVC_H

#include "types.h"

/* USB Video Class (UVC 1.0/1.1) — Webcam / Video Capture */

#define UVC_MAX_CAMERAS  4
#define UVC_MAX_FORMATS  8
#define UVC_FRAME_BUF_SIZE (640 * 480 * 3)

/* UVC request codes */
#define UVC_SET_CUR           0x01
#define UVC_GET_CUR           0x81
#define UVC_GET_MIN           0x82
#define UVC_GET_MAX           0x83
#define UVC_GET_RES           0x84
#define UVC_GET_LEN           0x85
#define UVC_GET_INFO          0x86

/* UVC interface subclass/protocol */
#define UVC_SC_VIDEOCONTROL   0x01
#define UVC_SC_VIDEOSTREAMING 0x02

/* UVC terminal types */
#define UVC_TT_STREAMING       0x0100
#define UVC_TT_CAMERA          0x0201

/* Video format descriptor types */
#define UVC_VS_FORMAT_UNCOMPRESSED  0x04
#define UVC_VS_FRAME_UNCOMPRESSED   0x05
#define UVC_VS_FORMAT_MJPEG         0x06
#define UVC_VS_FRAME_MJPEG          0x07

struct uvc_format {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint8_t  format;  /* 0=uncompressed, 1=MJPEG */
    uint8_t  bits_per_pixel;
    uint8_t  max_packet_mult;
};

struct uvc_camera {
    struct usb_dev *dev;
    int      ep_in;
    int      open;
    int      streaming;
    int      cur_width;
    int      cur_height;
    int      cur_fps;
    int      cur_format;
    int      num_formats;
    struct uvc_format formats[UVC_MAX_FORMATS];
    uint8_t  frame_buf[UVC_FRAME_BUF_SIZE] __attribute__((aligned(4096)));
    int      frame_ready;
    int      frame_size;
    uint8_t  alt_setting;
};

int  usb_uvc_init(void);
int  usb_uvc_available(void);
void usb_uvc_print_info(void);

int  usb_uvc_get_camera_count(void);
int  usb_uvc_get_format_count(int cam);
struct uvc_format *usb_uvc_get_format(int cam, int fmt);

int  usb_uvc_open(int cam);
int  usb_uvc_close(int cam);
int  usb_uvc_set_format(int cam, int width, int height, int fps);
int  usb_uvc_start_streaming(int cam);
int  usb_uvc_stop_streaming(int cam);

int  usb_uvc_read_frame(int cam, void *buf, int max_len);
int  usb_uvc_poll_frame(int cam);

#endif
