# [kanshi]

kanshi allows you to define output profiles that are automatically enabled and
disabled on hotplug. For instance, this can be used to turn a laptop's internal
screen off when docked.

This is a Wayland equivalent for tools like [autorandr]. kanshi can be used on
Wayland compositors supporting the wlr-output-management protocol.

Join the IRC channel: #emersion on Libera Chat.

## Building

Dependencies:

* wayland-client
* scdoc (optional, for man pages)

```sh
meson build
ninja -C build
```

## Usage

```sh
mkdir -p ~/.config/kanshi && touch ~/.config/kanshi/config
kanshi
```

## Configuration file

Each output profile is delimited by brackets. It contains several `output`
directives (whose syntax is similar to `sway-output(5)`). A profile will be
enabled if all of the listed outputs are connected.

```
profile {
	output LVDS-1 disable
	output "Some Company ASDF 4242" mode 1600x900 position 0,0
}

profile {
	output LVDS-1 enable scale 2
}
```

## Contributing

The upstream repository can be found [on SourceHut][repo]. Open tickets [on
the SourceHut tracker][issue-tracker]. Send patches
[on the mailing list][mailing-list] or open pull requests
[on the GitHub mirror][github].

## License

MIT

[kanshi]: https://wayland.emersion.fr/kanshi/
[autorandr]: https://github.com/phillipberndt/autorandr
[repo]: https://git.sr.ht/~emersion/kanshi
[issue-tracker]: https://todo.sr.ht/~emersion/kanshi
[mailing-list]: https://lists.sr.ht/~emersion/public-inbox
[github]: https://github.com/emersion/kanshi
