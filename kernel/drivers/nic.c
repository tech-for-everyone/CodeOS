#include "nic.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/spinlock.h"

#include "e1000.h"
#include "rtl8139.h"
#include "rtl8169.h"
#include "cdc_eth.h"
#include "virtio_net.h"
#include "usb_rndis.h"

static nic_driver_t *drivers[NIC_MAX];
static int num_drivers;
static nic_driver_t *active;
static uint8_t our_mac[6];
static int nic_up;
static spinlock_t nic_lock = SPINLOCK_INIT;

void nic_register(nic_driver_t *drv) {
    if (num_drivers >= NIC_MAX) return;
    drivers[num_drivers++] = drv;
}

static void build_driver_list(void) {
    nic_register(&e1000_nic);
    nic_register(&rtl8139_nic);
    nic_register(&rtl8169_nic);
    nic_register(&cdc_eth_nic);
    nic_register(&virtio_net_nic);
    nic_register(&usb_rndis_nic);
}

int nic_init(void) {
    build_driver_list();
    for (int i = 0; i < num_drivers; i++) {
        kprintf("nic: probing %s...\n", drivers[i]->name);
        if (drivers[i]->probe() == 0) {
            active = drivers[i];
            active->get_mac(our_mac);
            kprintf("nic: active = %s MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    active->name,
                    our_mac[0], our_mac[1], our_mac[2],
                    our_mac[3], our_mac[4], our_mac[5]);
            nic_up = 1;
            return 0;
        }
    }
    kprintf("nic: no NIC found\n");
    return -1;
}

int nic_send(const void *data, int len) {
    if (!nic_up || !active) return -1;
    spin_lock(&nic_lock);
    int r = active->send(data, len);
    spin_unlock(&nic_lock);
    return r;
}

int nic_recv(void *buf, int max_len) {
    if (!nic_up || !active) return -1;
    spin_lock(&nic_lock);
    int r = active->recv(buf, max_len);
    spin_unlock(&nic_lock);
    return r;
}

void nic_get_mac(uint8_t *mac) {
    memcpy(mac, our_mac, 6);
}

int nic_ready(void) {
    return nic_up;
}

const char *nic_name(void) {
    return active ? active->name : "none";
}
