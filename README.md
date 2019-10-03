# kanshi

kanshi allows you to define output profiles that are automatically enabled and
disabled on hotplug. For instance, this can be used to turn a laptop's internal
screen off when docked.

This is a Wayland equivalent for tools like [autorandr]. kanshi can be used on
Wayland compositors supporting the wlr-output-management protocol.

Join the IRC channel: ##emersion on Freenode.

## Building

Dependencies:

* wayland-client
* scdoc (optional, for man pages)
* libvarlink

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
{
	output LVDS-1 disable
	output "Some Company ASDF 4242" mode 1600x900 position 0,0
}

{
	output LVDS-1 enable scale 2
}
```

## License

MIT

[autorandr]: https://github.com/phillipberndt/autorandr
