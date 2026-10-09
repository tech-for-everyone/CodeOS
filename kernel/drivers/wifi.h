#ifndef WIFI_H
#define WIFI_H

#include "types.h"

#define WIFI_SSID_MAX     32
#define WIFI_BSSID_MAX    6
#define WIFI_PASS_MAX     64
#define WIFI_MAX_SCAN     32

typedef enum {
    WIFI_SEC_OPEN   = 0,
    WIFI_SEC_WEP    = 1,
    WIFI_SEC_WPA    = 2,
    WIFI_SEC_WPA2   = 3,
    WIFI_SEC_WPA3   = 4,
} wifi_security_t;

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    uint8_t bssid[WIFI_BSSID_MAX];
    uint8_t channel;
    int8_t rssi;
    wifi_security_t security;
    bool hidden;
} wifi_ap_t;

typedef struct {
    wifi_ap_t aps[WIFI_MAX_SCAN];
    int count;
    uint64_t scan_time;
} wifi_scan_result_t;

typedef enum {
    WIFI_DISCONNECTED = 0,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
} wifi_state_t;

typedef struct {
    const char *name;

    int (*probe)(void);
    int (*scan)(wifi_scan_result_t *out);
    int (*connect)(const char *ssid, const char *password, wifi_security_t sec);
    int (*disconnect)(void);
    wifi_state_t (*get_state)(void);
    int (*get_rssi)(void);
    void (*get_mac)(uint8_t *mac);
    void (*get_ip)(uint32_t *ip, uint32_t *gw, uint32_t *dns);
} wifi_driver_t;

void wifi_register(wifi_driver_t *drv);

int wifi_init(void);
int wifi_scan(wifi_scan_result_t *out);
int wifi_connect(const char *ssid, const char *password);
int wifi_disconnect(void);
wifi_state_t wifi_get_state(void);
int wifi_get_rssi(void);
void wifi_get_mac(uint8_t *mac);
void wifi_get_ip(uint32_t *ip, uint32_t *gw, uint32_t *dns);
const char *wifi_state_str(wifi_state_t state);
bool wifi_is_connected(void);

void wifi_virtio_init(void);

#endif