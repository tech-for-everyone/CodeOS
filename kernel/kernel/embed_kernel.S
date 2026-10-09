.section .rodata.embed_kernel, "a"
.balign 16
.global _binary_codeos_1_kernel_stage1_bin_start
.type _binary_codeos_1_kernel_stage1_bin_start, @object
_binary_codeos_1_kernel_stage1_bin_start:
.incbin "/home/codeosuser/CodeOS/kernel/codeos-1-kernel-stage1.bin"
.global _binary_codeos_1_kernel_stage1_bin_end
_binary_codeos_1_kernel_stage1_bin_end:
.global _binary_codeos_1_kernel_stage1_bin_size
_binary_codeos_1_kernel_stage1_bin_size:
.quad _binary_codeos_1_kernel_stage1_bin_end - _binary_codeos_1_kernel_stage1_bin_start
