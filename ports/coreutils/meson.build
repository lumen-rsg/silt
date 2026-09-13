coreutils_root = source_root / 'vendor/coreutils-9.11'
if not fs.is_file(coreutils_root / '.silt-prepared')
  error('run python3 tools/prepare_coreutils.py before configuring')
endif
coreutils_includes = include_directories('.', '../../include/posix', '../../vendor/coreutils-9.11/lib')
coreutils_args = silt_libc_c_args + ['-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-type-limits', '-Wno-unused-variable']
coreutils_commands = []
foreach name : ['echo', 'basename', 'dirname', 'cat', 'head', 'tail', 'wc', 'pwd', 'printenv', 'yes']
  coreutils_commands += static_library(
    'gnu-' + name,
    coreutils_root / 'src' / (name + '.c'),
    include_directories: coreutils_includes,
    c_args: coreutils_args + ['-Dmain=silt_' + name + '_main', '-Dusage=silt_' + name + '_usage'],
    install: false,
  )
endforeach
coreutils_support = static_library(
  'silt-coreutils-support',
  'support.c', 'dispatch.c', 'streams.c', 'read.c',
  coreutils_root / 'lib/basename.c',
  coreutils_root / 'lib/basename-lgpl.c',
  coreutils_root / 'lib/dirname-lgpl.c',
  coreutils_root / 'lib/stripslash.c',
  coreutils_root / 'lib/getopt.c',
  coreutils_root / 'lib/getopt1.c',
  coreutils_root / 'lib/xstrtoumax.c',
  coreutils_root / 'lib/xstrtoimax.c',
  coreutils_root / 'lib/xdectoumax.c',
  coreutils_root / 'lib/xdectoimax.c',
  coreutils_root / 'lib/full-read.c',
  coreutils_root / 'lib/full-write.c',
  coreutils_root / 'lib/safe-read.c',
  coreutils_root / 'lib/safe-write.c',
  coreutils_root / 'lib/exitfail.c',
  coreutils_root / 'lib/long-options.c',
  coreutils_root / 'lib/xgetcwd.c',
  coreutils_root / 'lib/root-dev-ino.c',

  include_directories: coreutils_includes,
  c_args: coreutils_args,
  install: false,
)
# Keep output at the build root, like all manifest application inputs.
