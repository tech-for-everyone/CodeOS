#ifndef ARP_H
#define ARP_H
#include "types.h"

#define ARP_HW_ETHER      1
#define ARP_OP_REQUEST    1
#define ARP_OP_REPLY      2

#define ARP_FLAG_GRATUITOUS 1
#define ARP_FLAG_PROXY      2

#define ARP_CACHE_SIZE   64
#define ARP_CACHE_TTL  300000

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
    uint64_t created;
    uint64_t last_access;
} arp_entry_t;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed)) arp_packet_t;

int  arp_init(void);
int  arp_resolve(uint32_t ip, uint8_t *mac);
void arp_update(uint32_t ip, const uint8_t *mac);
void arp_remove(uint32_t ip);
void arp_purge_expired(void);
void arp_send_request(uint32_t target_ip);
void arp_send_reply(uint32_t target_ip, const uint8_t *target_mac);
void arp_send_gratuitous(uint32_t ip);
void arp_handle_packet(const uint8_t *src_mac, const arp_packet_t *pkt, int len);
void arp_set_proxy(int enabled, uint32_t proxy_ip);
void arp_get_table(arp_entry_t *table, int *count);
void arp_dump(void);
int  arp_cache_count(void);
int  arp_cache_has_ip(uint32_t ip);
void arp_timer_tick(void);

#endif
