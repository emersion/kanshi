# kanshi

Kanshi uses a configuration file and a list of available displays to choose the
right settings for each display. It's useful if your window manager doesn't
support multiple display configurations (e.g. i3/Sway).

For now, it only supports:

* `sysfs` as backend
* Configuration file
	* GNOME (`~/.config/monitors.xml`)
	* Kanshi (see below)
* Sway as frontend

## Usage

```sh
cargo install kanshi
touch ~/.config/kanshi/config
kanshi > ~/.config/sway/outputs
```

### Configuration file

Each monitor configuration is delimited by brackets. Each line has the same
syntax as `sway(5)`.

```
{
	output LVDS-1 disable
	output VGA-1 resolution 1600x900 position 0,0
}

{
	output LVDS-1 vendor CMN product 0x1484 serial 0x0 resolution 1600x900 scale 2
}
```

## License

MIT
