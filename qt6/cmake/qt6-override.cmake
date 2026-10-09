# Override Qt6 architecture config test for CodeOS cross-compilation
# Qt6 uses try_run(try_run_result try_compile_result ...) which fails in 
# freestanding cross-compilation. We pre-define the results.

function(qt_internal_run_config_test_architecture result)
    set(TEST_architecture_sse2 TRUE CACHE INTERNAL "")
    set(TEST_architecture_ssse3 TRUE CACHE INTERNAL "")
    set(TEST_architecture_sse4_1 TRUE CACHE INTERNAL "")
    set(TEST_architecture_sse4_2 TRUE CACHE INTERNAL "")
    set(TEST_architecture_avx FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx2 FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512f FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512pf FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512er FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512cd FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512vl FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512bw FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512dq FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512ifma FALSE CACHE INTERNAL "")
    set(TEST_architecture_avx512vbmi FALSE CACHE INTERNAL "")
    set(TEST_architecture_aes FALSE CACHE INTERNAL "")
    set(TEST_architecture_rdrnd FALSE CACHE INTERNAL "")
    set(TEST_architecture_rdseed FALSE CACHE INTERNAL "")
    set(TEST_architecture_shani FALSE CACHE INTERNAL "")
    set(TEST_architecture_arch HASWELL CACHE INTERNAL "")
    set(${result} TRUE PARENT_SCOPE)
endfunction()

# Also override feature extraction test
function(qt_run_config_test_architecture)
    qt_internal_run_config_test_architecture(config_test_result)
endfunction()

# Disable all config.tests that require linking executables
set(__qt_pass_config_test TRUE CACHE INTERNAL "")
