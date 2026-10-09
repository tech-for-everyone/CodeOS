#include "audit.h"
#include "process.h"
#include "kprintf.h"
#include "string.h"

static audit_context_t audit_ctx;
static const char *type_names[] __attribute__((unused)) = {
    "","sys_exit","sys_call","file_open","file_close","file_read","file_write",
    "proc_exec","proc_exit","net_connect","net_bind","auth","mount","time_change"
};

void audit_init(void) {
    memset(&audit_ctx, 0, sizeof(audit_ctx));
    audit_ctx.enabled = 1;
    kprintf("audit: subsystem initialized (max %d rules, %d logs)\n", AUDIT_MAX_RULES, AUDIT_MAX_LOGS);
}

int audit_set_enabled(int enabled) { audit_ctx.enabled = enabled; return 0; }

int audit_add_rule(const audit_rule_t *rule) {
    if (!rule || audit_ctx.rule_count >= AUDIT_MAX_RULES) return -1;
    audit_ctx.rules[audit_ctx.rule_count++] = *rule;
    return audit_ctx.rule_count - 1;
}

int audit_del_rule(int index) {
    if (index < 0 || index >= audit_ctx.rule_count) return -1;
    for (int i = index; i < audit_ctx.rule_count - 1; i++)
        audit_ctx.rules[i] = audit_ctx.rules[i+1];
    audit_ctx.rule_count--;
    return 0;
}

int audit_log(int type, int success, const char *comm, const char *exe, const char *data) {
    if (!audit_ctx.enabled) return 0;
    spin_lock(&audit_ctx.lock);
    int dominated = 0;
    for (int i = 0; i < audit_ctx.rule_count; i++) {
        audit_rule_t *r = &audit_ctx.rules[i];
        if (!r->enabled) continue;
        if (r->syscall_num != 0 && r->syscall_num != type) continue;
        if (r->filter == AUDIT_FAILURE && success) continue;
        if (r->filter == AUDIT_SUCCESS && !success) continue;
        dominated = 1; break;
    }
    if (!dominated && audit_ctx.rule_count > 0) { spin_unlock(&audit_ctx.lock); return 0; }
    audit_record_t *rec = &audit_ctx.log[audit_ctx.log_tail];
    memset(rec, 0, sizeof(audit_record_t));
    rec->id = audit_ctx.next_id++;
    rec->timestamp = 0;
    rec->serial = (uint32_t)audit_ctx.next_id;
    rec->type = type;
    rec->success = success;
    rec->pid = current_process ? current_process->pid : 0;
    if (comm) strncpy_safe(rec->comm, comm, sizeof(rec->comm));
    if (exe) strncpy_safe(rec->exe, exe, sizeof(rec->exe));
    if (data) strncpy_safe(rec->data, data, sizeof(rec->data));
    audit_ctx.log_tail = (audit_ctx.log_tail + 1) % AUDIT_MAX_LOGS;
    if (audit_ctx.log_count < AUDIT_MAX_LOGS) audit_ctx.log_count++;
    else audit_ctx.log_head = (audit_ctx.log_head + 1) % AUDIT_MAX_LOGS;
    spin_unlock(&audit_ctx.lock);
    return 0;
}

int audit_read(audit_record_t *rec) {
    if (!rec) return -1;
    spin_lock(&audit_ctx.lock);
    if (audit_ctx.log_count == 0) { spin_unlock(&audit_ctx.lock); return -1; }
    *rec = audit_ctx.log[audit_ctx.log_head];
    audit_ctx.log_head = (audit_ctx.log_head + 1) % AUDIT_MAX_LOGS;
    audit_ctx.log_count--;
    spin_unlock(&audit_ctx.lock); return 0;
}

int audit_get_count(void) { return audit_ctx.log_count; }

void audit_dump(void) {
    spin_lock(&audit_ctx.lock);
    kprintf("audit: %d rules, %d logs, enabled=%d\n", audit_ctx.rule_count, audit_ctx.log_count, audit_ctx.enabled);
    for (int i = 0; i < audit_ctx.rule_count; i++) {
        audit_rule_t *r = &audit_ctx.rules[i];
        kprintf("  rule[%d]: syscall=%d filter=%d enabled=%d comm=%s\n",
                i, r->syscall_num, r->filter, r->enabled, r->comm);
    }
    spin_unlock(&audit_ctx.lock);
}
