# The ncvm portal device crate.  Builds the Rust memory-handler device
# and links it into the system emulator when CONFIG_X_NCVM_RUST is set
# (selected from the C `NCVM` Kconfig entry when HAVE_RUST is enabled).

_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common,
  c_args: bindgen_c_args,
)

_libncvm_rs = static_library(
  'ncvm',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/device.rs',
    ],
    {'.' : _bindings_inc_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    common_rs,
    util_rs,
    bql_rs,
    qom_rs,
    migration_rs,
    system_rs,
    hwcore_rs,
    glib_sys_rs,
  ],
)

rust_devices_ss.add(when: 'CONFIG_X_NCVM_RUST', if_true: [declare_dependency(
  link_whole: [_libncvm_rs],
  variables: {'crate': 'ncvm'},
)])