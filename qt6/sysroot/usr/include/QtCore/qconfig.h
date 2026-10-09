#ifndef QT_FEATURES_GlobalConfig_src_corelib_global_qconfig_h_H
#define QT_FEATURES_GlobalConfig_src_corelib_global_qconfig_h_H

#define QT_FEATURE_version_tagging 1

#define QT_FEATURE_shared -1

#define QT_FEATURE_static 1

#define QT_FEATURE_cross_compile 1

#define QT_FEATURE_pkg_config 1

#define QT_FEATURE_debug_and_release -1

#define QT_FEATURE_separate_debug_info -1

#define QT_FEATURE_appstore_compliant -1

#define QT_FEATURE_simulator_and_device -1

#define QT_FEATURE_rpath -1

#define QT_FEATURE_force_asserts -1

#define QT_FEATURE_framework -1

#define QT_FEATURE_cxx20 -1

#define QT_FEATURE_cxx2a -1

#define QT_FEATURE_cxx2b -1

#define QT_FEATURE_cxx2c -1

#define QT_FEATURE_reduce_relocations -1

#define QT_FEATURE_signaling_nan 1

#define QT_FEATURE_wasm_simd128 -1

#define QT_FEATURE_wasm_exceptions -1

#define QT_FEATURE_wasm_jspi -1

#define QT_FEATURE_zstd -1

#define QT_FEATURE_thread -1

#define QT_FEATURE_future 1

#define QT_FEATURE_concurrent 1

#define QT_FEATURE_dbus -1

#define QT_FEATURE_openssl_linked -1

#define QT_FEATURE_opensslv11 -1

#define QT_FEATURE_opensslv30 -1

#define QT_FEATURE_test_gui 1

#define QT_FEATURE_test_squish 1


#define QT_VERSION_STR "6.11.1"

#define QT_VERSION_MAJOR 6

#define QT_VERSION_MINOR 11

#define QT_VERSION_PATCH 1

#define QT_COPYRIGHT "Copyright (C) The Qt Company Ltd. and other contributors."
#if !defined(QT_SHARED) && !defined(QT_STATIC)
  #define QT_STATIC
#endif
#define QT_LARGEFILE_SUPPORT 64
#define QT_VISIBILITY_AVAILABLE
#define QT_NO_DBUS 1
#define QT_NO_OPENSSL
#define QT_GUI_TEST 1
#define QT_SQUISH_TEST

#endif // QT_FEATURES_GlobalConfig_src_corelib_global_qconfig_h_H
