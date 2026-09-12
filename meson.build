project(
  'silt-os',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'b_pie=false',
    'b_staticpic=false',
  ],
)

fs = import('fs')
python = find_program('python3')
cross_cc = find_program('c', native: false)
cross_ld = find_program('ld', native: false)
cross_nm = find_program('nm', native: false)
source_root = meson.current_source_dir()
dash_root = source_root / 'vendor/dash-0.5.13.5'
dash_src = dash_root / 'src'
neva_source_dir = get_option('neva_source_dir')
neva_build_dir = get_option('neva_build_dir')
neva_source_root = source_root / neva_source_dir
neva_build_root = source_root / neva_build_dir

if not fs.is_dir(dash_src) or not fs.is_file(dash_src / 'builtins.c')
  error(
    'dash sources are not prepared; run tools/prepare_dash.sh before meson setup',
  )
endif

if not fs.is_dir(neva_source_dir)
  warning('Neva source checkout not found at ' + neva_source_dir)
endif

neva_crt0 = neva_build_root / 'apps/crt0.o'
neva_libneva = neva_build_root / 'apps/libneva.a'
neva_user_linker = neva_source_root / 'apps/user.ld'
neva_formatter = neva_source_root / 'tools/mknevfs.py'
foreach required : [neva_crt0, neva_libneva, neva_user_linker, neva_formatter]
  if not fs.is_file(required)
    error('required Neva SDK artifact is missing: ' + required)
  endif
endforeach

dash_sources = files(
  'vendor/dash-0.5.13.5/src/alias.c',
  'vendor/dash-0.5.13.5/src/arith_yacc.c',
  'vendor/dash-0.5.13.5/src/arith_yylex.c',
  'vendor/dash-0.5.13.5/src/cd.c',
  'vendor/dash-0.5.13.5/src/error.c',
  'vendor/dash-0.5.13.5/src/eval.c',
  'vendor/dash-0.5.13.5/src/exec.c',
  'vendor/dash-0.5.13.5/src/expand.c',
  'vendor/dash-0.5.13.5/src/histedit.c',
  'vendor/dash-0.5.13.5/src/input.c',
  'vendor/dash-0.5.13.5/src/jobs.c',
  'vendor/dash-0.5.13.5/src/mail.c',
  'vendor/dash-0.5.13.5/src/main.c',
  'vendor/dash-0.5.13.5/src/memalloc.c',
  'vendor/dash-0.5.13.5/src/miscbltin.c',
  'vendor/dash-0.5.13.5/src/mystring.c',
  'vendor/dash-0.5.13.5/src/options.c',
  'vendor/dash-0.5.13.5/src/output.c',
  'vendor/dash-0.5.13.5/src/parser.c',
  'vendor/dash-0.5.13.5/src/redir.c',
  'vendor/dash-0.5.13.5/src/show.c',
  'vendor/dash-0.5.13.5/src/system.c',
  'vendor/dash-0.5.13.5/src/trap.c',
  'vendor/dash-0.5.13.5/src/var.c',
  'vendor/dash-0.5.13.5/src/bltin/printf.c',
  'vendor/dash-0.5.13.5/src/bltin/test.c',
  'vendor/dash-0.5.13.5/src/bltin/times.c',
  'vendor/dash-0.5.13.5/src/builtins.c',
  'vendor/dash-0.5.13.5/src/init.c',
  'vendor/dash-0.5.13.5/src/nodes.c',
  'ports/dash/signames.silt.c',
  'vendor/dash-0.5.13.5/src/syntax.c',
)

dash_includes = include_directories(
  'include/posix',
  'ports/dash',
  'vendor/dash-0.5.13.5/src',
  'vendor/dash-0.5.13.5/src/bltin',
)

dash_c_args = [
  '-march=armv8-a',
  '-ffreestanding',
  '-nostdlib',
  '-fno-builtin',
  '-DBSD=1',
  '-DSHELL=1',
  '-include', source_root / 'ports/dash/config.silt.h',
  '-Wno-unused-parameter',
  '-Wno-sign-compare',
  '-Wno-discarded-qualifiers',
]

dash_port_archive = static_library(
  'silt-dash-port',
  dash_sources,
  include_directories: dash_includes,
  c_args: dash_c_args,
  install: false,
)

silt_libc_sources = files(
  'libc/cleanup.c',
  'libc/ctype.c',
  'libc/descriptors.c',
  'libc/format.c',
  'libc/memory.c',
  'libc/process.c',
  'libc/runtime.c',
  'libc/setjmp.S',
  'libc/startup.c',
  'libc/terminal.c',
  'libc/crt0.S',
)
silt_libc_includes = include_directories(
  'include/posix',
  neva_source_dir / 'apps',
  neva_source_dir / 'ports',
)
silt_libc_c_args = [
  '-march=armv8-a',
  '-ffreestanding',
  '-nostdlib',
  '-fno-builtin',
  '-Os',
  '-Wall',
  '-Wextra',
  '-Wpedantic',
  '-Werror',
  '-std=c2x',
  '-fstack-protector-strong',
  '-ffile-prefix-map=' + source_root + '=.',
  '-fdebug-prefix-map=' + source_root + '=.',
]
silt_libc = static_library(
  'silt-libc',
  silt_libc_sources,
  include_directories: silt_libc_includes,
  c_args: silt_libc_c_args,
  install: false,
)

test(
  'dash-required-symbol-contract',
  find_program('tools/audit_dash_symbols.sh'),
  args: [
    dash_port_archive,
    files('ports/dash/required-symbols.txt'),
  ],
)

test(
  'rootfs-harness-framing',
  python,
  args: files('tools/test_rootfs_harness.py'),
)

test(
  'dash-linux-interrupt-reference',
  find_program('python3'),
  args: [files('tools/test_dash_interrupt.py'), source_root / 'vendor/dash-0.5.13.5/src/dash'],
)

silt_app_names = [
  'check-cleanup',
  'check-faults',
  'check-pager-smp',
  'check-pipes',
  'check-signals',
  'check-terminal',
  'echo',
  'false',
  'id',
  'meminfo',
  'ps',
  'sysinfo',
  'true',
  'uname',
  'whoami',
]
silt_app_c_args = [
  '-march=armv8-a',
  '-ffreestanding',
  '-nostdlib',
  '-Os',
  '-Wall',
  '-Wextra',
  '-Wpedantic',
  '-Werror',
  '-std=c2x',
  '-fstack-protector-strong',
  '-ffile-prefix-map=' + source_root + '=.',
  '-fdebug-prefix-map=' + source_root + '=.',
  '-I' + source_root / 'apps',
  '-I' + source_root / 'include/posix',
  '-I' + neva_source_root / 'apps',
  '-I' + neva_source_root / 'ports',
  '-I' + neva_source_root / 'services/initd/include',
]
silt_app_link_args = [
  '-z', 'max-page-size=4096',
  '-T', neva_user_linker,
  '--gc-sections',
  '-nostdlib',
  '-static',
]

dash_app = custom_target(
  'silt-dash-app',
  output: 'dash.elf',
  command: [cross_ld] + silt_app_link_args + [
    '--start-group', dash_port_archive, silt_libc, neva_libneva, '--end-group',
    '-o', '@OUTPUT@',
  ],
  depends: [dash_port_archive, silt_libc],
)

test(
  'dash-linked-symbol-closure',
  python,
  args: [files('tools/audit_dash_link.py'), '--nm', cross_nm.full_path(), dash_app],
)

silt_app_targets = {}
foreach app_name : silt_app_names
  app_extra_link_args = []
  app_link_inputs = [
    '@INPUT@', '--start-group', silt_libc, neva_libneva, '--end-group',
  ]
  if app_name == 'true'
    app_extra_link_args = ['--undefined=pager_exec_probe']
    # /bin/true is also Neva's bounded two-page PagerFile execution fixture.
    # Keep that dedicated probe image minimal; it does not use descriptors.
    app_link_inputs = [neva_crt0, '@INPUT@', neva_libneva]
  endif
  app_object = custom_target(
    'silt-' + app_name + '-object',
    input: 'apps/' + app_name + '.c',
    output: 'silt_' + app_name + '.o',
    depfile: 'silt_' + app_name + '.o.d',
    command: [
      cross_cc,
    ] + silt_app_c_args + [
      '-MMD', '-MQ', '@OUTPUT@', '-MF', '@DEPFILE@',
      '-c', '@INPUT@', '-o', '@OUTPUT@',
    ],
  )
  app_target = custom_target(
    'silt-' + app_name + '-app',
    input: app_object,
    output: app_name + '.elf',
    command: [cross_ld] + silt_app_link_args + app_extra_link_args
      + app_link_inputs + ['-o', '@OUTPUT@'],
  )
  silt_app_targets += {app_name: app_target}
endforeach

rootfs_manifest = files('rootfs/manifest.json')
rootfs_sources = files(
  'rootfs/boot/dash-d2.sh',
  'rootfs/etc/motd',
  'rootfs/etc/os-release',
)
rootfs_command = [
  python,
  files('tools/build_rootfs.py'),
  '--formatter', neva_formatter,
  '--manifest', rootfs_manifest,
  '--staging', source_root / 'rootfs',
  '--output', '@OUTPUT@',
]
rootfs_dependencies = []
rootfs_app_names = ['dash'] + silt_app_names
rootfs_app_targets = {'dash': dash_app} + silt_app_targets
foreach app_name : rootfs_app_names
  rootfs_command += ['--app', app_name, rootfs_app_targets[app_name]]
  rootfs_dependencies += rootfs_app_targets[app_name]
endforeach

silt_rootfs = custom_target(
  'silt-rootfs',
  input: rootfs_sources,
  output: 'silt-rootfs.img',
  command: rootfs_command,
  depends: rootfs_dependencies,
  build_by_default: true,
)

run_target(
  'rootfs-qemu-test',
  command: [
    python,
    files('tools/test_rootfs.py'),
    '--neva-source', neva_source_root,
    '--neva-build', neva_build_root,
    '--rootfs', silt_rootfs,
    '--smp', '4',
  ],
  depends: silt_rootfs,
)

run_target(
  'run',
  command: [
    python,
    files('tools/run_silt.py'),
    '--neva-source', neva_source_root,
    '--neva-build', neva_build_root,
    '--silt-build', meson.current_build_dir(),
    '--skip-build',
    '--console-tty',
  ],
  depends: silt_rootfs,
)

summary(
  {
    'dash version': '0.5.13.5',
    'dash mode': 'D4 interactive job-control bring-up',
    'job control': 'enabled through capability-scoped process groups and ttyd',
    'line editing': 'disabled (dash SMALL configuration)',
    'Neva source': neva_source_dir,
    'Neva build': neva_build_dir,
    'rootfs image': meson.current_build_dir() / 'silt-rootfs.img',
  },
  section: 'Silt port status',
)
