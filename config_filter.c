#include "config_filter.h"

#include <string.h>

int config_filter(const char *name)
{
	const char *const *p;

	static const char *const skip_prefix[] = {
		".",
		"~",
		"CVS",
		NULL
	};

	static const char *const skip_suffix[] = {
		".rpmsave",
		".rpmorig",
		".rpmnew",
		".dpkg-old",
		".dpkg-dist",
		".dpkg-new",
		".dpkg-bak",
		".bak",
		".orig",
		".rej",
		".YaST2save",
		".-",
		"~",
		",v",
		NULL
	};

	for (p = skip_prefix; *p; p++) {
		if (strncmp(name, *p, strlen(*p)) == 0)
			return 0;
	}

	for (p = skip_suffix; *p; p++) {
		if (strlen(name) >= strlen(*p) &&
		    strcmp(*p, strchr(name, 0) - strlen(*p)) == 0)
		    return 0;
 	}

	return 1;
}

