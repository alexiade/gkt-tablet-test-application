project('gtk-tablet-doodle', 'c',
  version: '0.1.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.60',
  default_options: ['c_std=gnu11', 'warning_level=1', 'buildtype=debugoptimized'])

if host_machine.system() != 'windows'
  error('gtk-tablet-doodle tests GTK\'s Windows tablet support; build it for Windows')
endif

cc = meson.get_compiler('c')
win32 = [cc.find_library('user32'), cc.find_library('m', required: false)]
inc = include_directories('src/common')
common = files('src/common/doodle.c', 'src/common/doodle-gtk.c', 'src/common/ring.c')

gtk3 = dependency('gtk+-3.0', version: '>= 3.24', required: get_option('gtk3'))
if gtk3.found()
  executable('doodle3', common, 'src/gtk3/doodle3.c',
    include_directories: inc, dependencies: [gtk3, win32], install: true)
endif

gtk4 = dependency('gtk4', version: '>= 4.12', required: get_option('gtk4'))
if gtk4.found()
  executable('doodle4', common, 'src/gtk4/doodle4.c',
    include_directories: inc, dependencies: [gtk4, win32], install: true)
endif
