#include "netstat.h"
#include "ip.h"
#include "ethernet.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "arp.h"
#include "kprintf.h"
#include "string.h"

void netstat_init(void) {
    kprintf("netstat: init\n");
}

int netstat_get(net_stats_t *out) {
    if (!out) return -1;
    ip_stats_t ips;
    eth_stats_t eths;
    icmp_stats_t icmps;
    udp_stats_t udps;
    ip_get_stats(&ips);
    eth_get_stats(&eths);
    icmp_get_stats(&icmps);
    udp_get_stats(&udps);
    memset(out, 0, sizeof(*out));
    out->ip.ip_in_receives = ips.rx_packets;
    out->ip.ip_in_hdr_errors = ips.bad_header;
    out->ip.ip_in_addr_errors = ips.bad_checksum;
    out->ip.ip_forw_datagrams = ips.forwarded;
    out->ip.ip_in_discards = ips.rx_dropped;
    out->ip.ip_in_delivers = ips.delivered;
    out->ip.ip_out_requests = ips.tx_packets;
    out->ip.ip_reasm_oks = ips.reassembled;
    out->ip.ip_frag_oks = ips.fragmented;
    out->udp.udp_in_datagrams = udps.rx_packets;
    out->udp.udp_out_datagrams = udps.tx_packets;
    out->udp.udp_no_port = udps.no_port;
    out->udp.udp_in_errors = udps.rx_errors;
    out->icmp.icmp_in_msgs = icmps.rx_msgs;
    out->icmp.icmp_out_msgs = icmps.tx_msgs;
    out->icmp.icmp_in_errors = icmps.rx_errors;
    out->icmp.icmp_in_echo_reps = icmps.rx_echo_reply;
    out->icmp.icmp_out_echos = icmps.tx_echo;
    out->icmp.icmp_in_dest_unreachs = icmps.rx_dest_unreach;
    out->icmp.icmp_in_time_excds = icmps.rx_time_exceeded;
    out->icmp.icmp_in_redirects = icmps.rx_redirect;
    return 0;
}

void netstat_dump_ip(void) {
    ip_stats_t s;
    ip_get_stats(&s);
    kprintf("IP: rx=%lu:%lu tx=%lu:%lu fwd=%lu drop=%lu err=%lu\n",
            s.rx_packets, s.rx_bytes, s.tx_packets, s.tx_bytes,
            s.forwarded, s.rx_dropped, s.rx_errors);
}

void netstat_dump_tcp(void) {
    kprintf("TCP: sockets=%d\n", tcp_socket_count());
}

void netstat_dump_udp(void) {
    udp_stats_t s;
    udp_get_stats(&s);
    kprintf("UDP: rx=%lu:%lu tx=%lu:%lu noport=%lu err=%lu\n",
            s.rx_packets, s.rx_bytes, s.tx_packets, s.tx_bytes,
            s.no_port, s.rx_errors);
}

void netstat_dump_icmp(void) {
    icmp_stats_t s;
    icmp_get_stats(&s);
    kprintf("ICMP: rx=%lu tx=%lu err=%lu timeout=%lu\n",
            s.rx_msgs, s.tx_msgs, s.rx_errors, s.timeout);
}

void netstat_dump_arp(void) {
    kprintf("ARP: cache=%d\n", arp_cache_count());
    arp_dump();
}

void netstat_dump(void) {
    kprintf("=== Network Statistics ===\n");
    netstat_dump_ip();
    netstat_dump_tcp();
    netstat_dump_udp();
    netstat_dump_icmp();
    netstat_dump_arp();
    eth_stats_t eths;
    eth_get_stats(&eths);
    kprintf("ETH: rx=%lu:%lu tx=%lu:%lu brd=%lu\n",
            eths.rx_packets, eths.rx_bytes, eths.tx_packets, eths.tx_bytes,
            eths.rx_broadcast);
}
