#include "wifi.h"
#include "kprintf.h"
#include "string.h"
#include "timer.h"
#include "net.h"
#include "virtio_net.h"
#include "../kernel/net_internal.h"

#define WIFI_DRIVER_MAX 4

static wifi_driver_t *wifi_drivers[WIFI_DRIVER_MAX];
static int wifi_driver_count = 0;
static wifi_driver_t *current_driver = NULL;
static wifi_scan_result_t last_scan;
static wifi_state_t current_state = WIFI_DISCONNECTED;

void wifi_register(wifi_driver_t *drv) {
    if (wifi_driver_count < WIFI_DRIVER_MAX) {
        wifi_drivers[wifi_driver_count++] = drv;
        kprintf("wifi: registered driver '%s'\n", drv->name);
    }
}

int wifi_init(void) {
    for (int i = 0; i < wifi_driver_count; i++) {
        if (wifi_drivers[i]->probe()) {
            current_driver = wifi_drivers[i];
            kprintf("wifi: using driver '%s'\n", current_driver->name);
            return 0;
        }
    }
    kprintf("wifi: no driver found\n");
    return -1;
}

int wifi_scan(wifi_scan_result_t *out) {
    if (!current_driver || !current_driver->scan) return -1;
    int ret = current_driver->scan(out);
    if (ret == 0) {
        memcpy(&last_scan, out, sizeof(wifi_scan_result_t));
    }
    return ret;
}

int wifi_connect(const char *ssid, const char *password) {
    if (!current_driver || !current_driver->connect) return -1;
    if (!ssid || !ssid[0]) return -1;

    wifi_security_t sec = WIFI_SEC_OPEN;
    if (password && password[0]) sec = WIFI_SEC_WPA2;

    current_state = WIFI_CONNECTING;
    int ret = current_driver->connect(ssid, password, sec);
    if (ret == 0) {
        current_state = WIFI_CONNECTED;
        kprintf("wifi: connected to '%s'\n", ssid);
    } else {
        current_state = WIFI_FAILED;
        kprintf("wifi: failed to connect to '%s'\n", ssid);
    }
    return ret;
}

int wifi_disconnect(void) {
    if (!current_driver || !current_driver->disconnect) return -1;
    int ret = current_driver->disconnect();
    current_state = WIFI_DISCONNECTED;
    return ret;
}

wifi_state_t wifi_get_state(void) {
    if (current_driver && current_driver->get_state) {
        current_state = current_driver->get_state();
    }
    return current_state;
}

int wifi_get_rssi(void) {
    if (current_driver && current_driver->get_rssi) {
        return current_driver->get_rssi();
    }
    return -100;
}

void wifi_get_mac(uint8_t *mac) {
    if (current_driver && current_driver->get_mac) {
        current_driver->get_mac(mac);
    }
}

void wifi_get_ip(uint32_t *ip, uint32_t *gw, uint32_t *dns) {
    if (current_driver && current_driver->get_ip) {
        current_driver->get_ip(ip, gw, dns);
    }
}

const char *wifi_state_str(wifi_state_t state) {
    switch (state) {
        case WIFI_DISCONNECTED: return "Disconnected";
        case WIFI_CONNECTING:   return "Connecting...";
        case WIFI_CONNECTED:    return "Connected";
        case WIFI_FAILED:       return "Failed";
    }
    return "Unknown";
}

bool wifi_is_connected(void) {
    return wifi_get_state() == WIFI_CONNECTED;
}

/* ─── Dummy driver for QEMU / virtio (uses existing net stack) ─── */

static int virtio_wifi_probe(void) {
    return virtio_net_init() == 0 ? 1 : 0;
}

static int virtio_wifi_scan(wifi_scan_result_t *out) {
    memset(out, 0, sizeof(wifi_scan_result_t));
    wifi_ap_t *ap = &out->aps[0];
    strcpy(ap->ssid, "QEMU_VirtIO");
    ap->bssid[0] = 0x52; ap->bssid[1] = 0x54; ap->bssid[2] = 0x00;
    ap->bssid[3] = 0x12; ap->bssid[4] = 0x34; ap->bssid[5] = 0x56;
    ap->channel = 1;
    ap->rssi = -30;
    ap->security = WIFI_SEC_OPEN;
    ap->hidden = false;
    out->count = 1;
    out->scan_time = timer_get_milliseconds();
    return 0;
}

static int virtio_wifi_connect(const char *ssid, const char *password, wifi_security_t sec) {
    (void)ssid; (void)password; (void)sec;
    return net_init();
}

static int virtio_wifi_disconnect(void) {
    return 0;
}

static wifi_state_t virtio_wifi_state(void) {
    return net_ready() ? WIFI_CONNECTED : WIFI_DISCONNECTED;
}

static int virtio_wifi_rssi(void) {
    return net_ready() ? -30 : -100;
}

static void virtio_wifi_mac(uint8_t *mac) {
    extern void nic_get_mac(uint8_t *mac);
    nic_get_mac(mac);
}

static void virtio_wifi_ip(uint32_t *ip, uint32_t *gw, uint32_t *dns) {
    if (ip) *ip = net_get_ip();
    if (gw) *gw = net_get_gateway();
    if (dns) *dns = net_get_dns();
}

static wifi_driver_t virtio_wifi_driver = {
    .name = "virtio-wifi",
    .probe = virtio_wifi_probe,
    .scan = virtio_wifi_scan,
    .connect = virtio_wifi_connect,
    .disconnect = virtio_wifi_disconnect,
    .get_state = virtio_wifi_state,
    .get_rssi = virtio_wifi_rssi,
    .get_mac = virtio_wifi_mac,
    .get_ip = virtio_wifi_ip,
};

void wifi_virtio_init(void) {
    wifi_register(&virtio_wifi_driver);
}