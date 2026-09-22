#ifndef DNS_H
#define DNS_H

#include "types.h"

/* ── DNS wire format constants ── */

#define DNS_PORT            53
#define DNS_MAX_NAME       253
#define DNS_MAX_LABEL       63
#define DNS_MAX_RESP      1024
#define DNS_CACHE_SIZE      64
#define DNS_MAX_SERVERS      3
#define DNS_DEFAULT_TTL  60000   /* 60 seconds in ms */
#define DNS_NEG_TTL      10000   /* 10 seconds for negative cache */
#define DNS_TIMEOUT_MS    2000   /* per-attempt timeout */
#define DNS_RETRIES          2   /* queries per server */
#define DNS_MAX_CNAME       16   /* max CNAME chain depth */
#define DNS_TCP_PORT       53    /* DNS over TCP port */

/* Header flags (network byte order) */
#define DNS_FLAG_RD     (1u << 8)   /* Recursion Desired */
#define DNS_FLAG_RA     (1u << 15)  /* Recursion Available */
#define DNS_FLAG_AA     (1u << 10)  /* Authoritative Answer */
#define DNS_FLAG_TC     (1u << 9)   /* Truncation */
#define DNS_RCODE_MASK  0x000F

/* Response codes */
#define DNS_RCODE_OK         0
#define DNS_RCODE_FORMERR    1
#define DNS_RCODE_SERVFAIL   2
#define DNS_RCODE_NXDOMAIN   3
#define DNS_RCODE_NOTIMP     4
#define DNS_RCODE_REFUSED    5

/* Record types */
#define DNS_TYPE_A       1
#define DNS_TYPE_NS      2
#define DNS_TYPE_CNAME   5
#define DNS_TYPE_SOA     6
#define DNS_TYPE_AAAA    28
#define DNS_TYPE_ANY     255

/* Record classes */
#define DNS_CLASS_IN     1

/* ── Public API ── */
void dns_init(void);
int  dns_resolve(const char *hostname, uint32_t *ip);
int  dns_resolve_ipv6(const char *hostname, uint8_t *ipv6);
void dns_set_server(uint32_t ip);
uint32_t dns_get_server(void);
const char *dns_get_server_str(void);
void dns_status(void);
void dns_cache_flush(void);
int  dns_cache_count(void);
int  dns_cache_size(void);
void dns_cache_dump(void);
void dns_remove(const char *hostname);

/* ── DNS header (12 bytes, wire format) ── */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

/* ── DNS cache entry ── */
typedef struct {
    char     name[253];
    uint32_t ip;
    uint8_t  ipv6[16];      /* AAAA record (IPv6) */
    uint8_t  has_ipv6;
    uint64_t last_access;   /* LRU timestamp */
    uint64_t expires;
    uint16_t query_id;
    int8_t   rcode;
    uint8_t  valid;
    uint8_t  is_cname;
} dns_cache_entry_t;

/* ── DNS server entry ── */
typedef struct {
    uint32_t ip;
    int      active;
} dns_server_t;

#endif
