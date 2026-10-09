#include "netdev.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

static netdev_t devs[NETDEV_MAX];
static int dev_count;
static int default_idx;
static spinlock_t nd_lock = SPINLOCK_INIT;

int netdev_init(void) {
    memset(devs, 0, sizeof(devs));
    dev_count = 0;
    default_idx = -1;
    kprintf("netdev: init %d slots\n", NETDEV_MAX);
    return 0;
}

int netdev_register(const char *name, int type, const netdev_t *ops) {
    if (!name || !ops) return -1;
    spin_lock(&nd_lock);
    for (int i = 0; i < NETDEV_MAX; i++) {
        if (!devs[i].name[0]) {
            memset(&devs[i], 0, sizeof(netdev_t));
            strncpy_safe(devs[i].name, name, NETDEV_NAME_LEN - 1);
            devs[i].type = type;
            devs[i].index = i;
            devs[i].mtu = 1500;
            devs[i].send = ops->send;
            devs[i].recv = ops->recv;
            devs[i].get_mac = ops->get_mac;
            devs[i].set_mtu = ops->set_mtu;
            devs[i].set_promisc = ops->set_promisc;
            devs[i].flags = NETDEV_UP | NETDEV_RUNNING;
            if (ops->get_mac) ops->get_mac(devs[i].addr);
            dev_count++;
            if (default_idx < 0) default_idx = i;
            spin_unlock(&nd_lock);
            kprintf("netdev: registered %s idx=%d type=%d\n", name, i, type);
            return i;
        }
    }
    spin_unlock(&nd_lock);
    return -1;
}

int netdev_unregister(const char *name) {
    if (!name) return -1;
    spin_lock(&nd_lock);
    for (int i = 0; i < NETDEV_MAX; i++) {
        if (strcmp(devs[i].name, name) == 0) {
            devs[i].name[0] = 0;
            dev_count--;
            if (default_idx == i) default_idx = -1;
            if (default_idx < 0) {
                for (int j = 0; j < NETDEV_MAX; j++)
                    if (devs[j].name[0]) { default_idx = j; break; }
            }
            spin_unlock(&nd_lock);
            return 0;
        }
    }
    spin_unlock(&nd_lock);
    return -1;
}

netdev_t *netdev_get(const char *name) {
    if (!name) return 0;
    spin_lock(&nd_lock);
    for (int i = 0; i < NETDEV_MAX; i++) {
        if (devs[i].name[0] && strcmp(devs[i].name, name) == 0) {
            spin_unlock(&nd_lock);
            return &devs[i];
        }
    }
    spin_unlock(&nd_lock);
    return 0;
}

netdev_t *netdev_get_by_index(int index) {
    if (index < 0 || index >= NETDEV_MAX) return 0;
    spin_lock(&nd_lock);
    netdev_t *r = devs[index].name[0] ? &devs[index] : 0;
    spin_unlock(&nd_lock);
    return r;
}

int netdev_get_count(void) {
    spin_lock(&nd_lock);
    int count = dev_count;
    spin_unlock(&nd_lock);
    return count;
}

int netdev_up(const char *name) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    spin_lock(&nd_lock);
    d->flags |= NETDEV_UP;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_down(const char *name) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    spin_lock(&nd_lock);
    d->flags &= ~NETDEV_UP;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_set_ip(const char *name, uint32_t ip) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    spin_lock(&nd_lock);
    d->ip_addr = ip;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_set_netmask(const char *name, uint32_t mask) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    spin_lock(&nd_lock);
    d->netmask = mask;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_set_gateway(const char *name, uint32_t gw) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    spin_lock(&nd_lock);
    d->gateway = gw;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_set_dns(const char *name, uint32_t dns) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    spin_lock(&nd_lock);
    d->dns = dns;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_set_mtu(const char *name, int mtu) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    if (d->set_mtu) { if (d->set_mtu(mtu) < 0) return -1; }
    spin_lock(&nd_lock);
    d->mtu = mtu;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_set_promisc(const char *name, int on) {
    netdev_t *d = netdev_get(name);
    if (!d) return -1;
    if (d->set_promisc) d->set_promisc(on);
    spin_lock(&nd_lock);
    if (on) d->flags |= NETDEV_PROMISC;
    else d->flags &= ~NETDEV_PROMISC;
    spin_unlock(&nd_lock);
    return 0;
}

int netdev_send(const char *name, const void *data, int len) {
    int index;
    int (*send)(const void *, int);
    spin_lock(&nd_lock);
    index = name ? -1 : default_idx;
    for (int i = 0; name && i < NETDEV_MAX; i++)
        if (devs[i].name[0] && strcmp(devs[i].name, name) == 0) { index = i; break; }
    if (index < 0 || !(devs[index].flags & NETDEV_UP) || !devs[index].send) {
        spin_unlock(&nd_lock); return -1;
    }
    send = devs[index].send;
    spin_unlock(&nd_lock);
    int r = send(data, len);
    spin_lock(&nd_lock);
    if (r >= 0) { devs[index].tx_packets++; devs[index].tx_bytes += r; }
    else devs[index].tx_errors++;
    spin_unlock(&nd_lock);
    return r;
}

int netdev_recv(const char *name, void *buf, int max_len) {
    int index;
    int (*recv)(void *, int);
    spin_lock(&nd_lock);
    index = name ? -1 : default_idx;
    for (int i = 0; name && i < NETDEV_MAX; i++)
        if (devs[i].name[0] && strcmp(devs[i].name, name) == 0) { index = i; break; }
    if (index < 0 || !(devs[index].flags & NETDEV_UP) || !devs[index].recv) {
        spin_unlock(&nd_lock); return -1;
    }
    recv = devs[index].recv;
    spin_unlock(&nd_lock);
    int r = recv(buf, max_len);
    spin_lock(&nd_lock);
    if (r > 0) { devs[index].rx_packets++; devs[index].rx_bytes += r; }
    else if (r < 0) devs[index].rx_errors++;
    spin_unlock(&nd_lock);
    return r;
}

void netdev_get_stats(const char *name, uint64_t *rxp, uint64_t *txp, uint64_t *rxb, uint64_t *txb) {
    netdev_t *d = netdev_get(name);
    if (!d) return;
    spin_lock(&nd_lock);
    if (rxp) *rxp = d->rx_packets;
    if (txp) *txp = d->tx_packets;
    if (rxb) *rxb = d->rx_bytes;
    if (txb) *txb = d->tx_bytes;
    spin_unlock(&nd_lock);
}

void netdev_dump(void) {
    kprintf("Network Devices (%d):\n", dev_count);
    for (int i = 0; i < NETDEV_MAX; i++) {
        if (!devs[i].name[0]) continue;
        kprintf("  %s idx=%d flags=0x%x mtu=%d ip=%08x\n",
                devs[i].name, devs[i].index, devs[i].flags, devs[i].mtu, devs[i].ip_addr);
        kprintf("    mac=%02x:%02x:%02x:%02x:%02x:%02x rx=%lu tx=%lu\n",
                devs[i].addr[0],devs[i].addr[1],devs[i].addr[2],
                devs[i].addr[3],devs[i].addr[4],devs[i].addr[5],
                devs[i].rx_packets, devs[i].tx_packets);
    }
}

int netdev_ready(const char *name) {
    spin_lock(&nd_lock);
    int index = name ? -1 : default_idx;
    for (int i = 0; name && i < NETDEV_MAX; i++)
        if (devs[i].name[0] && strcmp(devs[i].name, name) == 0) { index = i; break; }
    int ready = index >= 0 &&
        (devs[index].flags & (NETDEV_UP | NETDEV_RUNNING)) == (NETDEV_UP | NETDEV_RUNNING);
    spin_unlock(&nd_lock);
    return ready;
}

netdev_t *netdev_default(void) {
    spin_lock(&nd_lock);
    netdev_t *r = (default_idx >= 0) ? &devs[default_idx] : 0;
    spin_unlock(&nd_lock);
    return r;
}

void netdev_set_default(const char *name) {
    if (!name) return;
    spin_lock(&nd_lock);
    for (int i = 0; i < NETDEV_MAX; i++) {
        if (devs[i].name[0] && strcmp(devs[i].name, name) == 0) { default_idx = i; break; }
    }
    spin_unlock(&nd_lock);
}
