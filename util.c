#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logging.h"
#include "util.h"

/*
 * Read one logical line from a configuration file.
 *
 * Line endings may be escaped with backslashes, to form one logical line from
 * several physical lines.  No end of line character(s) are included in the
 * result.
 *
 * If linenum is not NULL, it is incremented by the number of physical lines
 * which have been read.
 */
char *getline_wrapped(FILE *file, unsigned int *linenum)
{
	int size = 256;
	int i = 0;
	char *buf = NOFAIL(malloc(size));
	for(;;) {
		int ch = getc_unlocked(file);

		switch(ch) {
		case EOF:
			if (i == 0) {
				free(buf);
				return NULL;
			}
			/* else fall through */

		case '\n':
			if (linenum)
				(*linenum)++;
			if (i == size)
				buf = NOFAIL(realloc(buf, size + 1));
			buf[i] = '\0';
			return buf;

		case '\\':
			ch = getc_unlocked(file);

			if (ch == '\n') {
				if (linenum)
					(*linenum)++;
				continue;
			}
			/* else fall through */

		default:
			buf[i++] = ch;

			if (i == size) {
				size *= 2;
				buf = NOFAIL(realloc(buf, size));
			}
		}
	}
}

/*
 * Convert filename to the module name.  Works if filename == modname, too.
 */
void filename2modname(char *modname, const char *filename)
{
	const char *afterslash;
	unsigned int i;

	afterslash = my_basename(filename);

	/* Convert to underscores, stop at first . */
	for (i = 0; afterslash[i] && afterslash[i] != '.'; i++) {
		if (afterslash[i] == '-')
			modname[i] = '_';
		else
			modname[i] = afterslash[i];
	}
	modname[i] = '\0';
}

/*
 * Replace dashes with underscores.
 * Dashes inside character range patterns (e.g. [0-9]) are left unchanged.
 */
char *underscores(char *string)
{
	unsigned int i;

	if (!string)
		return NULL;

	for (i = 0; string[i]; i++) {
		switch (string[i]) {
		case '-':
			string[i] = '_';
			break;

		case ']':
			warn("Unmatched bracket in %s\n", string);
			break;

		case '[':
			i += strcspn(&string[i], "]");
			if (!string[i])
				warn("Unmatched bracket in %s\n", string);
			break;
		}
	}
	return string;
}

/*
 * Find the next string in an ELF section.
 */
const char *next_string(const char *string, unsigned long *secsize)
{
	/* Skip non-zero chars */
	while (string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}

	/* Skip any zero padding. */
	while (!string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}
	return string;
}
