#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <elf.h>
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

/*
 * Get CPU endianness. 0 = unknown, 1 = ELFDATA2LSB = little, 2 = ELFDATA2MSB = big
 */
int __attribute__ ((pure)) native_endianness()
{
	/* Encoding the endianness enums in a string and then reading that
	 * string as a 32-bit int, returns the correct endianness automagically.
	 */
	return (char) *((uint32_t*)("\1\0\0\2"));
}

/*
 * Check ELF file header.
 */
int elf_ident(void *file, unsigned long fsize, int *conv)
{
	/* "\177ELF" <byte> where byte = 001 for 32-bit, 002 for 64 */
	unsigned char *ident = file;

	if (fsize < EI_CLASS || memcmp(file, ELFMAG, SELFMAG) != 0)
		return -ENOEXEC;	/* Not an ELF object */
	if (ident[EI_DATA] == 0 || ident[EI_DATA] > 2)
		return -EINVAL;		/* Unknown endianness */

	if (conv != NULL)
		*conv = native_endianness() != ident[EI_DATA];
	return ident[EI_CLASS];
}
