project('live-wayland-reaction', 'c',
  version : '0.2.0',
  default_options : [
    'warning_level=3',
    'b_lto=true',
  ]
)

cc = meson.get_compiler('c')

add_global_arguments('-DPROJECT_NAME="live-wayland-reaction"', language : 'c')
add_global_arguments('-DPROJECT_VERSION="0.2.0"', language : 'c')

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
