# Purple-Youtube

Purple-YouTube is a plugin for [Pidgin 3](https://pidgin.im/) that makes it possible to connect, read,
and post messages in YouTube live chats from within Pidgin.

This is an unofficial plugin not affiliated with or endorsed by either Pidgin or YouTube.

**Note**: Pidgin 3 and this plugin are currently alpha software and are not currently meant for non-techical users.

## Building

There are two ways to build the plugin: Meson or Flatpak. Both require you to clone this repository.

Flatpak is the easiest because it builds the plugin within a sandbox and integrates well with Pidgin 3's
use of Flatpak

### Flatpak

From the root directory of this repository, run:

```
flatpak run org.flatpak.Builder --user --force-clean --sandbox --install-deps-from=flathub build-flatpak --install im.pidgin.Pidgin3.Plugin.PurpleYoutube.yml
```

This will build and install the Purple-YouTube plugin onto your system in a place where Pidgin 3 can find it,
(assuming Pidgin3 is also built using Flatpak) It will also handle installing all dependencies.

### Meson

From the root directory of this reposition run:

```
meson setup build
ninja -C build
```

This locally builds the plugin and a demo program in a local directory named `build/`. Note that it expects
all dependencies are pre-installed on your system.

## License

GPLv3 or later
