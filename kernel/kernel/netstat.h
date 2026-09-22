#ifndef NETSTAT_H
#define NETSTAT_H
#include "types.h"

typedef struct {
    uint64_t tcp_active_opens;
    uint64_t tcp_passive_opens;
    uint64_t tcp_attempt_fails;
    uint64_t tcp_estab_resets;
    uint64_t tcp_curr_estab;
    uint64_t tcp_in_segs;
    uint64_t tcp_out_segs;
    uint64_t tcp_retrans_segs;
    uint64_t tcp_in_errs;
    uint64_t tcp_out_rsts;
    uint64_t tcp_alloc_fail;
} tcp_netstat_t;

typedef struct {
    uint64_t udp_in_datagrams;
    uint64_t udp_out_datagrams;
    uint64_t udp_no_errors;
    uint64_t udp_no_port;
    uint64_t udp_in_errors;
    uint64_t udp_rcvbuf_errors;
    uint64_t udp_sndbuf_errors;
} udp_netstat_t;

typedef struct {
    uint64_t ip_in_receives;
    uint64_t ip_in_hdr_errors;
    uint64_t ip_in_addr_errors;
    uint64_t ip_forw_datagrams;
    uint64_t ip_in_unknown_protos;
    uint64_t ip_in_discards;
    uint64_t ip_in_delivers;
    uint64_t ip_out_requests;
    uint64_t ip_routing_discards;
    uint64_t ip_out_discards;
    uint64_t ip_out_no_routes;
    uint64_t ip_reasm_timeout;
    uint64_t ip_reasm_reqds;
    uint64_t ip_reasm_oks;
    uint64_t ip_reasm_fails;
    uint64_t ip_frag_oks;
    uint64_t ip_frag_fails;
    uint64_t ip_frag_creates;
} ip_netstat_t;

typedef struct {
    uint64_t icmp_in_msgs;
    uint64_t icmp_in_errors;
    uint64_t icmp_in_dest_unreachs;
    uint64_t icmp_in_time_excds;
    uint64_t icmp_in_parm_probs;
    uint64_t icmp_in_redirects;
    uint64_t icmp_in_echos;
    uint64_t icmp_in_echo_reps;
    uint64_t icmp_in_timestamps;
    uint64_t icmp_in_timestamp_reps;
    uint64_t icmp_out_msgs;
    uint64_t icmp_out_errors;
    uint64_t icmp_out_dest_unreachs;
    uint64_t icmp_out_echos;
    uint64_t icmp_out_echo_reps;
    uint64_t icmp_out_redirects;
} icmp_netstat_t;

typedef struct {
    tcp_netstat_t tcp;
    udp_netstat_t udp;
    ip_netstat_t  ip;
    icmp_netstat_t icmp;
    uint64_t      arp_in_requests;
    uint64_t      arp_in_replies;
    uint64_t      arp_out_requests;
    uint64_t      arp_out_replies;
    uint64_t      arp_in_errors;
} net_stats_t;

void netstat_init(void);
int netstat_get(net_stats_t *stats);
void netstat_dump(void);
void netstat_dump_tcp(void);
void netstat_dump_udp(void);
void netstat_dump_ip(void);
void netstat_dump_icmp(void);
void netstat_dump_arp(void);

#endif
