#ifndef AD_BLOCK_H
#define AD_BLOCK_H

#include "types.h"

#define ADBLOCK_MAX_DOMAINS  256
#define ADBLOCK_DOMAIN_MAX   80
#define ADBLOCK_LIST_PATH    "/.adblock/list"

void adblock_init(void);
int  adblock_enable(void);
int  adblock_disable(void);
int  adblock_is_enabled(void);
int  adblock_check_host(const char *host);
int  adblock_reload(void);
void adblock_stats(void);
void adblock_list(void);
void adblock_status(void);
int  adblock_domain_count(void);
void adblock_record_dns_block(void);
void adblock_record_http_block(void);
int  adblock_add_domain(const char *domain);
int  adblock_remove_domain(const char *domain);

#endif
