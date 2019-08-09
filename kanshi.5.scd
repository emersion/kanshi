kanshi(5)

# NAME

kanshi - configuration file

# DESCRIPTION

A kanshi configuration file is a list of profiles. Each profile starts with an
optional name and contains directives delimited by brackets (*{* and *}*).

Example:

```
{
	output LVDS-1 disable
	output "Some Company ASDF 4242" mode 1600x900 position 0,0
}

nomad {
	output LVDS-1 enable scale 2
}
```

# DIRECTIVES

Directives are followed by space-separated arguments. Arguments can be quoted
(with *"*) if they contain spaces.

*output* <criteria> <output-command...>
	An output directive adds an output to the profile. The criteria can either
	be an output name, an output description or "\*". The latter can be used to
	match any output.

	On *sway*(1), output names and descriptions can be obtained via
	*swaymsg -t get_outputs*.

# OUTPUT DIRECTIVES

*enable*|*disable*
	Enables or disables the specified output.

*mode* <width>x<height>[@<rate>[Hz]]
	Configures the specified output to use the specified mode. Modes are a
	combination of width and height (in pixels) and a refresh rate (in Hz) that
	your display can be configured to use.

	Examples:

		output HDMI-A-1 mode 1920x1080
		output HDMI-A-1 mode 1920x1080@60Hz

*position* <x>,<y>
	Places the output at the specified position in the global coordinates space.

	Example:

		output HDMI-A-1 position 1600,0

*scale* <factor>
	Scales the output by the specified scale factor.

*transform* <transform>
	Sets the output transform. Can be one of "90", "180", "270" for a rotation;
	or "flipped", "flipped-90", "flipped-180", "flipped-270" for a rotation and
	a flip; or "normal" for no transform.


# AUTHORS

Maintained by Simon Ser <contact@emersion.fr>, who is assisted by other
open-source contributors. For more information about kanshi development, see
https://github.com/emersion/kanshi.

# SEE ALSO

*kanshi*(1)
