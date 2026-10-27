#include "cpu.h"
#include "../kernel/kprintf.h"

static uint32_t cpuid_leaf = 0;
static char cpu_vendor[13] = {0};
static char hypervisor_vendor[13] = {0};
static int has_hypervisor_flag = 0;

void cpu_detect_hypervisor(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // Get vendor string
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    *(uint32_t*)(cpu_vendor + 0) = ebx;
    *(uint32_t*)(cpu_vendor + 4) = edx;
    *(uint32_t*)(cpu_vendor + 8) = ecx;
    cpu_vendor[12] = 0;
    
    // Check for hypervisor bit
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    has_hypervisor_flag = (ecx & (1 << 31)) ? 1 : 0;
    
    if (has_hypervisor_flag) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000));
        if (eax >= 0x40000000) {
            *(uint32_t*)(hypervisor_vendor + 0) = ebx;
            *(uint32_t*)(hypervisor_vendor + 4) = ecx;
            *(uint32_t*)(hypervisor_vendor + 8) = edx;
            hypervisor_vendor[12] = 0;
            kprintf("cpu: hypervisor detected: %s\n", hypervisor_vendor);
        }
        if (cpu_is_kvm()) {
            kprintf("cpu: running on KVM (virtio/KVM optimized)\n");
        } else if (cpu_is_hyperv()) {
            kprintf("cpu: running on Hyper-V\n");
        } else if (cpu_is_vmware()) {
            kprintf("cpu: running on VMware\n");
        } else if (cpu_is_xen()) {
            kprintf("cpu: running on Xen\n");
        }
    }
}

int cpu_has_hypervisor(void) {
    return has_hypervisor_flag;
}

const char *cpu_get_hypervisor_vendor(void) {
    if (!has_hypervisor_flag) return NULL;
    return hypervisor_vendor;
}

int cpu_is_kvm(void) {
    if (!has_hypervisor_flag) return 0;
    return (hypervisor_vendor[0] == 'K' && hypervisor_vendor[1] == 'V' && 
            hypervisor_vendor[2] == 'M' && hypervisor_vendor[3] == 'K');
}

int cpu_is_hyperv(void) {
    if (!has_hypervisor_flag) return 0;
    return (hypervisor_vendor[0] == 'M' && hypervisor_vendor[1] == 'i' && 
            hypervisor_vendor[2] == 'c');
}

int cpu_is_vmware(void) {
    if (!has_hypervisor_flag) return 0;
    return (hypervisor_vendor[0] == 'V' && hypervisor_vendor[1] == 'M' && 
            hypervisor_vendor[2] == 'w');
}

int cpu_is_xen(void) {
    if (!has_hypervisor_flag) return 0;
    return (hypervisor_vendor[0] == 'X' && hypervisor_vendor[1] == 'e' && 
            hypervisor_vendor[2] == 'n');
}
