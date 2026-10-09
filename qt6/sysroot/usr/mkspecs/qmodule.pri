host_build {
    QT_CPU_FEATURES.x86_64 = 
} else {
    QT_CPU_FEATURES.x86_64 = 
}
QT.global_private.enabled_features = gc_binaries reduce_exports gui widgets intelcet glibc_fortify_source trivial_auto_var_init_pattern stack_protector stack_clash_protection libstdcpp_assertions cross_compile largefile
QT.global_private.disabled_features = use_bfd_linker use_gold_linker use_lld_linker use_mold_linker android-style-assets developer-build private_tests doc_snippets debug elf_private_full_version no_direct_extern_access x86intrin sse2 sse3 ssse3 sse4_1 sse4_2 avx f16c avx2 avx512f avx512er avx512cd avx512pf avx512dq avx512bw avx512vl avx512ifma avx512vbmi avx512vbmi2 aesni vaes rdrnd rdseed shani lsx lasx mips_dsp mips_dspr2 neon arm_crc32 arm_crypto arm_sve localtime_r localtime_s posix_fallocate force-system-libs force-bundled-libs system-zlib stdlib-libcpp dbus dbus-linked network printsupport sql testlib xml libudev openssl dlopen relocatable libcpp_hardening relro_now_linker android_16kb_pages
CONFIG += cross_compile largefile
PKG_CONFIG_EXECUTABLE = /usr/bin/pkg-config
QT_COORD_TYPE = double
QT_BUILD_PARTS = libs

