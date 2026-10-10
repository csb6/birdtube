# BirdTube

BirdTube is a plugin for [Pidgin 3](https://pidgin.im/) that makes it possible to interact with YouTube live chats from within Pidgin.

This is an unofficial plugin not affiliated with or endorsed by either Pidgin or YouTube.

## Dependencies

- [GObject](https://docs.gtk.org/gobject/)
- [GLib](https://docs.gtk.org/glib/)
- [Libsoup](https://libsoup.gnome.org/libsoup-3.0/)
- Purple 3 (Pidgin is a graphical frontend for this library)
- [Peel](https://gitlab.gnome.org/bugaevc/peel)
- [GOAuth](https://codeberg.org/csb6/goauth)

## Building

There are two ways to build the plugin once you have cloned the repository: Meson or Flatpak.

Flatpak is the easiest because it builds the plugin within a sandbox and integrates well with Pidgin 3's use of Flatpak

### Flatpak

From the root directory of this repository, run:

```
flatpak run org.flatpak.Builder --user --force-clean --sandbox --install-deps-from=flathub build-flatpak --install im.pidgin.Pidgin3.Plugin.BirdTube.yml
```

This will build the BirdTube plugin and install it onto your system in a place where Pidgin 3 can find it,
assuming Pidgin3 is also built using Flatpak. It will also install all dependencies.

### Meson

From the root directory of this repository run:

```
meson setup build
ninja -C build
```

This locally builds the plugin and a demo program in a local directory named `build`.

## License

GPLv3 or later
