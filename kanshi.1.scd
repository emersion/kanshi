kanshi(1)

# NAME

kanshi - dynamic output configuration

# DESCRIPTION

*kanshi* [options...]

# OPTIONS

*-h, --help*
	Show help message and quit.

*-c, --config* <config>
	Specifies a config file.

# SYNOPSIS

kanshi is a Wayland daemon that automatically configures outputs.

kanshi is configured with a list of output profiles. Each profile contains a set
of outputs. A profile will be automatically activated if all specified outputs
are currently connected. A profile contains configuration for each output.

If kanshi receives a SIGHUP signal, it will reread its config file.

# CONFIGURATION

kanshi reads its configuration from *$XDG_CONFIG_HOME/kanshi/config*. If unset,
*$XDG_CONFIG_HOME* defaults to *~/.config*.

An error is raised if no configuration file is found.

For information on the configuration file format, see *kanshi*(5).

# AUTHORS

Maintained by Simon Ser <contact@emersion.fr>, who is assisted by other
open-source contributors. For more information about kanshi development, see
https://github.com/emersion/kanshi.

# SEE ALSO

*kanshi*(5) *kanshictl*(1)
