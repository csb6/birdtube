project('youtube-chat-client', 'c', 'cpp',
        default_options: ['c_std=c99', 'warning_level=2', 'cpp_std=c++23'],
        meson_version: '>=1.2.0')

if get_option('log-http')
  add_project_arguments('-DYOUTUBE_CHAT_CLIENT_LOGGING', language: ['c', 'cpp'])
endif

gobject = dependency('gobject-2.0', required: true)
libsoup = dependency('libsoup-3.0', required: true)
json = dependency('json-glib-1.0', required: true)
librest_proj = subproject('rest-1.0',
                          default_options: {'vapi': false, 'examples': false, 'gtk_doc': false, 'tests': false})
librest = librest_proj.get_variable('librest_dep')
librest_gir = librest_proj.get_variable('librest_gir')
gio = dependency('gio-2.0', required: true)
purple = dependency('purple-3', required: true)
gplugin = dependency('gplugin', required: true)
peel = dependency('peel', required: true)

peel_gen = find_program('peel-gen')
peel_codegen = custom_target('peel-codegen',
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
  output: ['peel'],
  env: {
    'GI_GIR_PATH': 'subprojects/rest-1.0/rest:/usr/lib/x86_64-linux-gnu/gir-1.0:/app/3rd-party/PurpleYoutube/share/gir-1.0'
  },
  depends: [librest_gir],
)

plugin_install_dir = get_option('prefix') / get_option('libdir') / 'purple-3'

purple_youtube = shared_library('purple-youtube', [
    'src/youtube_chat_plugin.cpp',
    'src/youtube_chat_protocol.cpp',
    'src/youtube_chat_connection.cpp',
    'src/youtube_chat_client.cpp',
    'src/youtube_chat_parser.cpp',
    'src/one_shot_server.cpp',
    peel_codegen
  ],
  cpp_pch: 'src/pch/pch.hpp',
  dependencies: [gobject, libsoup, json, librest, gio, purple, gplugin, peel],
  install: true,
  install_dir: plugin_install_dir,
)

executable('yt-chat-demo', [
      'demo/demo.cpp',
      peel_codegen
    ],
    dependencies: [gobject, peel],
    include_directories: ['src'],
    link_with: [purple_youtube]
)
