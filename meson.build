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
cc = meson.get_compiler('c')
source_root = meson.current_source_dir()
dash_root = source_root / 'vendor/dash-0.5.13.5'
dash_src = dash_root / 'src'
neva_source_dir = get_option('neva_source_dir')

if not fs.is_dir(dash_src) or not fs.is_file(dash_src / 'builtins.c')
  error(
    'dash sources are not prepared; run tools/prepare_dash.sh before meson setup',
  )
endif

if not fs.is_dir(neva_source_dir)
  warning('Neva source checkout not found at ' + neva_source_dir)
endif

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
  '-mgeneral-regs-only',
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

test(
  'dash-required-symbol-contract',
  find_program('tools/audit_dash_symbols.sh'),
  args: [
    dash_port_archive,
    files('ports/dash/required-symbols.txt'),
  ],
)

summary(
  {
    'dash version': '0.5.13.5',
    'dash mode': 'compile-readiness archive',
    'job control': 'disabled until the Silt POSIX adapter is ready',
    'line editing': 'disabled (dash SMALL configuration)',
    'Neva source': neva_source_dir,
  },
  section: 'Silt port status',
)
