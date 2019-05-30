# kanshi

Kanshi uses a configuration file and a list of available displays to choose the
right settings for each display. It's useful if your window manager doesn't
support multiple display configurations (e.g. i3/Sway).

Join the IRC channel: ##emersion on Freenode.

## Usage

```sh
mkdir -p ~/.config/kanshi && touch ~/.config/kanshi/config
kanshi
```

### Configuration file

Each monitor configuration is delimited by brackets. Each line has the same
syntax as `sway-output(5)`.

```
{
	output LVDS-1 disable
	output VGA-1 resolution 1600x900 position 0,0
}

{
	output LVDS-1 resolution 1600x900 scale 2
}
```

## License

MIT
