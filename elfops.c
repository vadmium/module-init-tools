/* The nasty work of reading 32 and 64-bit modules is in here. */
#include <elf.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "depmod.h"
#include "util.h"
#include "logging.h"
#include "elfops.h"
#include "tables.h"
#include "zlibsupport.h"

#include "testing.h"

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

/*
 * grab_elf_file - read ELF file into memory
 * @pathame: file to load
 *
 * Returns NULL, and errno set on error.
 */
struct elf_file *grab_elf_file(const char *pathname)
{
	int fd;
	int err;
	struct elf_file *file;

	fd = open(pathname, O_RDONLY, 0);
	if (fd < 0)
		return NULL;
	file = grab_elf_file_fd(pathname, fd);

	err = errno;
	close(fd);
	errno = err;
	return file;
}

/*
 * grab_elf_file_fd - read ELF file from file descriptor into memory
 * @pathame: name of file to load
 * @fd: file descriptor of file to load
 *
 * Returns NULL, and errno set on error.
 */
struct elf_file *grab_elf_file_fd(const char *pathname, int fd)
{
	struct elf_file *file;

	file = malloc(sizeof(*file));
	if (!file) {
		errno = ENOMEM;
		return NULL;
	}
	file->pathname = strdup(pathname);
	if (!file->pathname) {
		free(file);
		errno = ENOMEM;
		return NULL;
	}
	file->data = grab_fd(fd, &file->len);
	if (!file->data)
		goto fail;

	switch (elf_ident(file->data, file->len, &file->conv)) {
	case ELFCLASS32:
		file->ops = &mod_ops32;
		break;
	case ELFCLASS64:
		file->ops = &mod_ops64;
		break;
	case -ENOEXEC: /* Not an ELF object */
	case -EINVAL: /* Unknown endianness */
	default: /* Unknown word size */
		errno = ENOEXEC;
		goto fail;
	}
	return file;
fail:
	release_elf_file(file);
	return NULL;
}

void release_elf_file(struct elf_file *file)
{
	int err = errno;

	if (!file)
		return;

	release_file(file->data, file->len);
	free(file->pathname);
	free(file);

	errno = err;
}
