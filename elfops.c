/* The nasty work of reading 32 and 64-bit modules is in here. */
#include <elf.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "depmod.h"
#include "util.h"
#include "logging.h"
#include "elfops.h"
#include "tables.h"

/* Symbol types, returned by load_dep_syms */
static const char *weak_sym = "W";
static const char *undef_sym = "U";

#define ELF32BIT
#include "elfops_core.c"
#undef ELF32BIT

#define ELF64BIT
#include "elfops_core.c"
#undef ELF64BIT

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

void *get_section(void *file, unsigned long filesize,
		  const char *secname, unsigned long *secsize)
{
	int conv;

	switch (elf_ident(file, filesize, &conv)) {
	case ELFCLASS32:
		return get_section32(file, filesize, secname, secsize, conv);
	case ELFCLASS64:
		return get_section64(file, filesize, secname, secsize, conv);
	default:
		return NULL;
	}
}
