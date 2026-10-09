host_build {
    QT_ARCH = x86_64
    QT_BUILDABI = 
    QT_TARGET_ARCH = x86_64
    QT_TARGET_BUILDABI = 
} else {
    QT_ARCH = x86_64
    QT_BUILDABI = 
    QT_LIBCPP_ABI_TAG = 
}
QT.global.enabled_features = version_tagging static cross_compile pkg-config signaling_nan future concurrent test_gui test_squish static cross_compile intelcet glibc_fortify_source trivial_auto_var_init_pattern stack_protector stack_clash_protection libstdcpp_assertions static reduce_exports
QT.global.disabled_features = shared debug_and_release separate_debug_info appstore-compliant simulator_and_device rpath force_asserts framework c++20 c++2a c++2b c++2c reduce_relocations wasm-simd128 wasm-exceptions wasm-jspi zstd thread dbus openssl-linked opensslv11 opensslv30
QT.global.disabled_features += release build_all
QT_CONFIG += static reduce_exports release
CONFIG += release  static cross_compile plugin_manifest intelcet glibc_fortify_source trivial_auto_var_init_pattern stack_protector stack_clash_protection libstdcpp_assertions
QT_VERSION = 6.11.1
QT_MAJOR_VERSION = 6
QT_MINOR_VERSION = 11
QT_PATCH_VERSION = 1

QT_GCC_MAJOR_VERSION = 16
QT_GCC_MINOR_VERSION = 1
QT_GCC_PATCH_VERSION = 0
