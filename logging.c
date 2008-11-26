#define _GNU_SOURCE /* asprintf */

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "logging.h"

/* Do we use syslog for messages or stderr? */
int logging = 0;

void message(const char *prefix, const char *fmt, va_list *arglist)
{
	int ret;
	char *buf, *buf2;

	ret = vasprintf(&buf, fmt, *arglist);
	if (ret >= 0)
		ret = asprintf(&buf2, "%s%s", prefix, buf);

	if (ret < 0)
		buf2 = "FATAL: Out of memory.\n";

	if (logging)
		syslog(LOG_NOTICE, "%s", buf2);
	else
		fprintf(stderr, "%s", buf2);

	if (ret < 0)
		exit(1);

	free(buf2);
	free(buf);
}

int warned = 0;
void warn(const char *fmt, ...)
{
	va_list arglist;
	warned++;
	va_start(arglist, fmt);
	message("WARNING: ", fmt, &arglist);
	va_end(arglist);
}

void fatal(const char *fmt, ...)
{
	va_list arglist;
	va_start(arglist, fmt);
	message("FATAL: ", fmt, &arglist);
	va_end(arglist);
	exit(1);
}

