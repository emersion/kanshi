kanshi(1)

# NAME

kanshi - dynamic output configuration

# SYNOPSIS

*kanshi* [-1] [-p _profile_]

# DESCRIPTION

kanshi is a Wayland daemon that automatically configures outputs.

kanshi is configured with a list of output profiles. Each profile contains a set
of outputs. A profile will be automatically activated if all specified outputs
are currently connected. A profile contains configuration for each output.

# OPTIONS

*-h*
	Show help message and quit.

*-p* _profile_
	Always use _profile_ for configuration; instead of automatically selecting
	one based on connected devices.

*-1*
	Do output configuration only once then quit. Return status will indicate
	whether configuration was successful or not. Default beahivor is to run
	indefinitely.

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

*kanshi*(5)
