#include <stdlib.h>

#include "parser.h"

int main(int argc, char *argv[]) {
	struct kanshi_config *config = parse_config("/home/simon/.config/kanshi/config");
	if (config == NULL) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
