import sys, os

input_path = sys.argv[1]
output_path = sys.argv[2]

with open(input_path, 'rb') as f:
    data = f.read()

abin = os.path.abspath(input_path)

with open(output_path, 'w') as f:
    f.write('.section .rodata.embed_kernel, "a"\n')
    f.write('.balign 16\n')
    f.write('.global _binary_codeos_1_kernel_stage1_bin_start\n')
    f.write('.type _binary_codeos_1_kernel_stage1_bin_start, @object\n')
    f.write('_binary_codeos_1_kernel_stage1_bin_start:\n')
    f.write('.incbin "{}"\n'.format(abin))
    f.write('.global _binary_codeos_1_kernel_stage1_bin_end\n')
    f.write('_binary_codeos_1_kernel_stage1_bin_end:\n')
    f.write('.global _binary_codeos_1_kernel_stage1_bin_size\n')
    f.write('_binary_codeos_1_kernel_stage1_bin_size:\n')
    f.write('.quad _binary_codeos_1_kernel_stage1_bin_end - _binary_codeos_1_kernel_stage1_bin_start\n')
