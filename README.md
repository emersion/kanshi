# kanshi

Kanshi uses a configuration file and a list of available displays to choose the
right settings for each display. It's useful if your window manager doesn't
support multiple display configurations (e.g. i3/Sway).

For now, it only supports:
* `sysfs` as backend
* `~/.config/monitors.xml` as configuration file (used by GNOME)
* Sway as output format

## Usage

```
cargo install kanshi
kanshi > ~/.config/sway/outputs
```

## License

MIT
