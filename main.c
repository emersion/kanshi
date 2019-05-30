#define _POSIX_C_SOURCE 1
#include <limits.h>
#include <stdlib.h>

#include "parser.h"

int main(int argc, char *argv[]) {
	const char config_filename[] = "kanshi/config";
	char config_path[PATH_MAX];
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_config_home != NULL) {
		snprintf(config_path, sizeof(config_path), "%s/%s",
			xdg_config_home, config_filename);
	} else if (home != NULL) {
		snprintf(config_path, sizeof(config_path), "%s/.config/%s",
			home, config_filename);
	} else {
		fprintf(stderr, "HOME not set\n");
		return EXIT_FAILURE;
	}

	struct kanshi_config *config = parse_config(config_path);
	if (config == NULL) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
