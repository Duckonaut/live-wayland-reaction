project('live-wayland-reaction', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

cc = meson.get_compiler('c')

add_global_arguments('-DPROJECT_NAME="${PROJECT_NAME}"', language : 'c')

src = [
  'src/main.c',
]

wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols')
subdir('protocols')

deps = [
  wayland_client,
  wayland_protocols,
  client_protos,
  cc.find_library('m', required : false)
]

stb = include_directories('stb', is_system : true)

exe = executable('live-wayland-reaction', src,
  include_directories : [
    stb
  ],
  dependencies : deps,
  install : true)
