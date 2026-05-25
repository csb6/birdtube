project('youtube-chat-client', 'c', 'cpp',
        default_options: ['c_std=c99', 'warning_level=2', 'cpp_std=c++23'],
        meson_version: '>=1.1.0')

if get_option('log-http')
  add_project_arguments('-DYOUTUBE_CHAT_CLIENT_LOGGING', language: ['c', 'cpp'])
endif

gobject = dependency('gobject-2.0', required: true)
libsoup = dependency('libsoup-3.0', required: true)
json = dependency('json-glib-1.0', required: true)
librest = dependency('rest-1.0', required: true)
gio = dependency('gio-2.0', required: true)
purple = dependency('purple-3', required: true)
gplugin = dependency('gplugin', required: true)
peel = dependency('peel', required: true)

peel_gen = find_program('peel-gen')
custom_target('peel-codegen',
  command: [
    peel_gen,
    '--recursive',
    '--out-dir', '@OUTDIR@',
    '--api-tweaks', '@SOURCE_ROOT@/src/api-tweaks.txt',
    'Rest', '1.0',
    'Json', '1.0',
    'Purple', '3.0',
  ],
  input: 'src/api-tweaks.txt',
  output: 'peel-codegen',
  env: {
    'GI_GIR_PATH': '/usr/lib/x86_64-linux-gnu/gir-1.0:/app/3rd-party/PurpleYoutube/share/gir-1.0'
  },
)

purple_youtube = shared_library('purple-youtube', [
    'src/youtube_chat_plugin.cpp',
    'src/youtube_chat_connection.cpp',
    'src/youtube_chat_client.cpp',
    'src/youtube_chat_parser.cpp',
    'src/one_shot_server.cpp',
  ],
  cpp_pch: 'src/pch/pch.hpp',
  dependencies: [gobject, libsoup, json, librest, gio, peel, purple, gplugin],
)

executable('yt-chat-demo', ['demo/demo.cpp'],
    dependencies: [gobject, peel],
    include_directories: ['src'],
    link_with: [purple_youtube]
)
