
#ifndef CPU_H
#define CPU_H

#include "../kernel/types.h"

#define CPUID_VENDOR_AMD           "AuthenticAMD"
#define CPUID_VENDOR_INTEL         "GenuineIntel"
#define CPUID_VENDOR_HYPERVISOR    "Microsoft Hv"
#define CPUID_VENDOR_KVM           "KVMKVMKVM\0\0\0"
#define CPUID_VENDOR_VMWARE        "VMwareVMware"
#define CPUID_VENDOR_XEN           "XenVMMXenVMM"
#define CPUID_VENDOR_BHYVE         "bhyve bhyve "
#define CPUID_VENDOR_VIRTUALBOX    "VBoxVBoxVBox"
#define CPUID_VENDOR_QEMU          "TCGTCGTCGTCG"

int cpu_has_hypervisor(void);
const char *cpu_get_hypervisor_vendor(void);
int cpu_is_kvm(void);
int cpu_is_hyperv(void);
int cpu_is_vmware(void);
int cpu_is_xen(void);

#endif
