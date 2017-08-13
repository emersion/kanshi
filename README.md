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

```
cargo install kanshi
kanshi > ~/.config/sway/outputs
```

## License

MIT
