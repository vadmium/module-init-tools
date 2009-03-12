/* modprobe.c: add or remove a module from the kernel, intelligently.
    Copyright (C) 2001  Rusty Russell.
    Copyright (C) 2002, 2003  Rusty Russell, IBM Corporation.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#define _GNU_SOURCE /* asprintf */

#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <elf.h>
#include <getopt.h>
#include <fnmatch.h>
#include <asm/unistd.h>
#include <sys/wait.h>
#include <syslog.h>

#define streq(a,b) (strcmp((a),(b)) == 0)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#include "zlibsupport.h"
#include "logging.h"
#include "index.h"
#include "list.h"
#include "config_filter.h"

#include "testing.h"

int use_binary_indexes = 1; /* default to enabled. */

extern long init_module(void *, unsigned long, const char *);
extern long delete_module(const char *, unsigned int);

struct module {
	struct list_head list;
	char *modname;
	char filename[0];
};

#ifndef MODULE_DIR
#define MODULE_DIR "/lib/modules"
#endif

typedef void (*errfn_t)(const char *fmt, ...);

static void grammar(const char *cmd, const char *filename, unsigned int line)
{
	warn("%s line %u: ignoring bad line starting with '%s'\n",
	     filename, line, cmd);
}

static void print_usage(const char *progname)
{
	fprintf(stderr,
		"Usage: %s [-v] [-V] [-C config-file] [-d <dirname> ] [-n] [-i] [-q] [-b] [-o <modname>] [ --dump-modversions ] <modname> [parameters...]\n"
		"%s -r [-n] [-i] [-v] <modulename> ...\n"
		"%s -l -t <dirname> [ -a <modulename> ...]\n",
		progname, progname, progname);
	exit(1);
}

static char *getline_wrapped(FILE *file, unsigned int *linenum)
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

static struct module *find_module(const char *filename, struct list_head *list)
{
	struct module *i;

	list_for_each_entry(i, list, list) {
		if (strcmp(i->filename, filename) == 0)
			return i;
	}
	return NULL;
}

/* Convert filename to the module name.  Works if filename == modname, too. */
static void filename2modname(char *modname, const char *filename)
{
	const char *afterslash;
	unsigned int i;

	afterslash = strrchr(filename, '/');
	if (!afterslash)
		afterslash = filename;
	else
		afterslash++;

	/* Convert to underscores, stop at first . */
	for (i = 0; afterslash[i] && afterslash[i] != '.'; i++) {
		if (afterslash[i] == '-')
			modname[i] = '_';
		else
			modname[i] = afterslash[i];
	}
	modname[i] = '\0';
}

/* We used to lock with a write flock but that allows regular users to block
 * module load by having a read lock on the module file (no way to bust the
 * existing locks without killing the offending process). Instead, we now
 * do the system call/init_module and allow the kernel to fail us instead.
 */
static int open_file(const char *filename)
{
	int fd = open(filename, O_RDONLY, 0);

	return fd;
}

static void close_file(int fd)
{
	/* Valgrind is picky... */
	close(fd);
}

static void add_module(char *filename, int namelen, struct list_head *list)
{
	struct module *mod;

	/* If it's a duplicate: move it to the end, so it gets
	   inserted where it is *first* required. */
	mod = find_module(filename, list);
	if (mod)
		list_del(&mod->list);
	else {
		/* No match.  Create a new module. */
		mod = NOFAIL(malloc(sizeof(struct module) + namelen + 1));
		memcpy(mod->filename, filename, namelen);
		mod->filename[namelen] = '\0';
		mod->modname = NOFAIL(malloc(namelen + 1));
		filename2modname(mod->modname, mod->filename);
	}

	list_add_tail(&mod->list, list);
}

/* Compare len chars of a to b, with _ and - equivalent. */
static int modname_equal(const char *a, const char *b, unsigned int len)
{
	unsigned int i;

	if (strlen(b) != len)
		return 0;

	for (i = 0; i < len; i++) {
		if ((a[i] == '_' || a[i] == '-')
		    && (b[i] == '_' || b[i] == '-'))
			continue;
		if (a[i] != b[i])
			return 0;
	}
	return 1;
}

/* Fills in list of modules if this is the line we want. */
static int add_modules_dep_line(char *line,
				const char *name,
				struct list_head *list,
				const char *dirname)
{
	char *ptr;
	int len;
	char *modname, *fullpath;

	/* Ignore lines without : or which start with a # */
	ptr = strchr(line, ':');
	if (ptr == NULL || line[strspn(line, "\t ")] == '#')
		return 0;

	/* Is this the module we are looking for? */
	*ptr = '\0';
	if (strrchr(line, '/'))
		modname = strrchr(line, '/') + 1;
	else
		modname = line;

	len = strlen(modname);
	if (strchr(modname, '.'))
		len = strchr(modname, '.') - modname;
	if (!modname_equal(modname, name, len))
		return 0;

	/* Create the list. */
	if ('/' == line[0]) {	/* old style deps - absolute path specified */
		add_module(line, ptr - line, list);
	} else {
		nofail_asprintf(&fullpath, "%s/%s", dirname, line);
		add_module(fullpath, strlen(dirname)+1+(ptr - line), list);
		free(fullpath);
	}

	ptr++;
	for(;;) {
		char *dep_start;
		ptr += strspn(ptr, " \t");
		if (*ptr == '\0')
			break;
		dep_start = ptr;
		ptr += strcspn(ptr, " \t");
		if ('/' == dep_start[0]) {	/* old style deps */
			add_module(dep_start, ptr - dep_start, list);
		} else {
			nofail_asprintf(&fullpath, "%s/%s", dirname, dep_start);
			add_module(fullpath,
				   strlen(dirname)+1+(ptr - dep_start), list);
			free(fullpath);
		}
	}
	return 1;
}

static int read_depends_file(const char *dirname,
			     const char *start_name,
			     struct list_head *list)
{
	char *modules_dep_name;
	char *line;
	struct index_file *modules_dep;

	nofail_asprintf(&modules_dep_name, "%s/%s", dirname, "modules.dep.bin");
	modules_dep = index_file_open(modules_dep_name);
	if (!modules_dep) {
		free(modules_dep_name);
		return 0;
	}

	line = index_search(modules_dep, start_name);
	if (line) {
		/* Value is standard dependency line format */
		if (!add_modules_dep_line(line, start_name, list, dirname))
			fatal("Module index is inconsistent\n");
		free(line);
	}

	index_file_close(modules_dep);
	free(modules_dep_name);
	
	return 1;
}

static void read_depends(const char *dirname,
			 const char *start_name,
			 struct list_head *list)
{
	char *modules_dep_name;
	char *line;
	FILE *modules_dep;
	int done = 0;

	if (use_binary_indexes)
		if (read_depends_file(dirname, start_name, list))
			return;

	nofail_asprintf(&modules_dep_name, "%s/%s", dirname, "modules.dep");
	modules_dep = fopen(modules_dep_name, "r");
	if (!modules_dep)
		fatal("Could not load %s: %s\n",
		      modules_dep_name, strerror(errno));

	/* Stop at first line, as we can have duplicates (eg. symlinks
           from boot/ */
	while (!done && (line = getline_wrapped(modules_dep, NULL)) != NULL) {
		done = add_modules_dep_line(line, start_name, list, dirname);
		free(line);
	}
	fclose(modules_dep);
	free(modules_dep_name);
}

/* We use error numbers in a loose translation... */
static const char *insert_moderror(int err)
{
	switch (err) {
	case ENOEXEC:
		return "Invalid module format";
	case ENOENT:
		return "Unknown symbol in module, or unknown parameter (see dmesg)";
	case ENOSYS:
		return "Kernel does not have module support";
	default:
		return strerror(err);
	}
}

static const char *remove_moderror(int err)
{
	switch (err) {
	case ENOENT:
		return "No such module";
	case ENOSYS:
		return "Kernel does not have module unloading support";
	default:
		return strerror(err);
	}
}

static void replace_modname(struct module *module,
			    void *mem, unsigned long len,
			    const char *oldname, const char *newname)
{
	char *p;

	/* 64 - sizeof(unsigned long) - 1 */
	if (strlen(newname) > 55)
		fatal("New name %s is too long\n", newname);

	/* Find where it is in the module structure.  Don't assume layout! */
	for (p = mem; p < (char *)mem + len - strlen(oldname); p++) {
		if (memcmp(p, oldname, strlen(oldname)) == 0) {
			strcpy(p, newname);
			return;
		}
	}

	warn("Could not find old name in %s to replace!\n", module->filename);
}

static void *get_section32(void *file,
			   unsigned long size,
			   const char *name,
			   unsigned long *secsize)
{
	Elf32_Ehdr *hdr = file;
	Elf32_Shdr *sechdrs = file + hdr->e_shoff;
	const char *secnames;
	unsigned int i;

	/* Too short? */
	if (size < sizeof(*hdr))
		return NULL;
	if (size < hdr->e_shoff + hdr->e_shnum * sizeof(sechdrs[0]))
		return NULL;
	if (size < sechdrs[hdr->e_shstrndx].sh_offset)
		return NULL;
		
	secnames = file + sechdrs[hdr->e_shstrndx].sh_offset;
	for (i = 1; i < hdr->e_shnum; i++)
		if (strcmp(secnames + sechdrs[i].sh_name, name) == 0) {
			*secsize = sechdrs[i].sh_size;
			return file + sechdrs[i].sh_offset;
		}
	return NULL;
}

static void *get_section64(void *file,
			   unsigned long size,
			   const char *name,
			   unsigned long *secsize)
{
	Elf64_Ehdr *hdr = file;
	Elf64_Shdr *sechdrs = file + hdr->e_shoff;
	const char *secnames;
	unsigned int i;

	/* Too short? */
	if (size < sizeof(*hdr))
		return NULL;
	if (size < hdr->e_shoff + hdr->e_shnum * sizeof(sechdrs[0]))
		return NULL;
	if (size < sechdrs[hdr->e_shstrndx].sh_offset)
		return NULL;
		
	secnames = file + sechdrs[hdr->e_shstrndx].sh_offset;
	for (i = 1; i < hdr->e_shnum; i++)
		if (strcmp(secnames + sechdrs[i].sh_name, name) == 0) {
			*secsize = sechdrs[i].sh_size;
			return file + sechdrs[i].sh_offset;
		}
	return NULL;
}

static int elf_ident(void *mod, unsigned long size)
{
	/* "\177ELF" <byte> where byte = 001 for 32-bit, 002 for 64 */
	char *ident = mod;

	if (size < EI_CLASS || memcmp(mod, ELFMAG, SELFMAG) != 0)
		return ELFCLASSNONE;
	return ident[EI_CLASS];
}

static void *get_section(void *file,
			 unsigned long size,
			 const char *name,
			 unsigned long *secsize)
{
	switch (elf_ident(file, size)) {
	case ELFCLASS32:
		return get_section32(file, size, name, secsize);
	case ELFCLASS64:
		return get_section64(file, size, name, secsize);
	default:
		return NULL;
	}
}

static void rename_module(struct module *module,
			  void *mod,
			  unsigned long len,
			  const char *newname)
{
	void *modstruct;
	unsigned long modstruct_len;

	/* Old-style */
	modstruct = get_section(mod, len, ".gnu.linkonce.this_module",
				&modstruct_len);
	/* New-style */
	if (!modstruct)
		modstruct = get_section(mod, len, "__module", &modstruct_len);
	if (!modstruct)
		warn("Could not find module name to change in %s\n",
		     module->filename);
	else
		replace_modname(module, modstruct, modstruct_len,
				module->modname, newname);
}

/* Kernel told to ignore these sections if SHF_ALLOC not set. */
static void invalidate_section32(void *mod, const char *secname)
{
	Elf32_Ehdr *hdr = mod;
	Elf32_Shdr *sechdrs = mod + hdr->e_shoff;
	const char *secnames = mod + sechdrs[hdr->e_shstrndx].sh_offset;
	unsigned int i;

	for (i = 1; i < hdr->e_shnum; i++)
		if (strcmp(secnames+sechdrs[i].sh_name, secname) == 0)
			sechdrs[i].sh_flags &= ~SHF_ALLOC;
}

static void invalidate_section64(void *mod, const char *secname)
{
	Elf64_Ehdr *hdr = mod;
	Elf64_Shdr *sechdrs = mod + hdr->e_shoff;
	const char *secnames = mod + sechdrs[hdr->e_shstrndx].sh_offset;
	unsigned int i;

	for (i = 1; i < hdr->e_shnum; i++)
		if (strcmp(secnames+sechdrs[i].sh_name, secname) == 0)
			sechdrs[i].sh_flags &= ~(unsigned long long)SHF_ALLOC;
}

static void strip_section(struct module *module,
			  void *mod,
			  unsigned long len,
			  const char *secname)
{
	switch (elf_ident(mod, len)) {
	case ELFCLASS32:
		invalidate_section32(mod, secname);
		break;
	case ELFCLASS64:
		invalidate_section64(mod, secname);
		break;
	default:
		warn("Unknown module format in %s: not forcing version\n",
		     module->filename);
	}
}

static const char *next_string(const char *string, unsigned long *secsize)
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

static void clear_magic(struct module *module, void *mod, unsigned long len)
{
	const char *p;
	unsigned long modlen;

	/* Old-style: __vermagic section */
	strip_section(module, mod, len, "__vermagic");

	/* New-style: in .modinfo section */
	for (p = get_section(mod, len, ".modinfo", &modlen);
	     p;
	     p = next_string(p, &modlen)) {
		if (strncmp(p, "vermagic=", strlen("vermagic=")) == 0) {
			memset((char *)p, 0, strlen(p));
			return;
		}
	}
}

struct module_options
{
	struct module_options *next;
	char *modulename;
	char *options;
};

struct module_command
{
	struct module_command *next;
	char *modulename;
	char *command;
};

struct module_alias
{
	struct module_alias *next;
	char *module;
};

struct module_blacklist
{
	struct module_blacklist *next;
	char *modulename;
};

/* Link in a new option line from the config file. */
static struct module_options *
add_options(const char *modname,
	    const char *option,
	    struct module_options *options)
{
	struct module_options *new;
	char *tab; 

	new = NOFAIL(malloc(sizeof(*new)));
	new->modulename = NOFAIL(strdup(modname));
	new->options = NOFAIL(strdup(option));
	/* We can handle tabs, kernel can't. */
	for (tab = strchr(new->options, '\t'); tab; tab = strchr(tab, '\t'))
		*tab = ' ';
	new->next = options;
	return new;
}

/* Link in a new install line from the config file. */
static struct module_command *
add_command(const char *modname,
	       const char *command,
	       struct module_command *commands)
{
	struct module_command *new;

	new = NOFAIL(malloc(sizeof(*new)));
	new->modulename = NOFAIL(strdup(modname));
	new->command = NOFAIL(strdup(command));
	new->next = commands;
	return new;
}

/* Link in a new alias line from the config file. */
static struct module_alias *
add_alias(const char *modname, struct module_alias *aliases)
{
	struct module_alias *new;

	new = NOFAIL(malloc(sizeof(*new)));
	new->module = NOFAIL(strdup(modname));
	new->next = aliases;
	return new;
}

/* Link in a new blacklist line from the config file. */
static struct module_blacklist *
add_blacklist(const char *modname, struct module_blacklist *blacklist)
{
	struct module_blacklist *new;

	new = NOFAIL(malloc(sizeof(*new)));
	new->modulename = NOFAIL(strdup(modname));
	new->next = blacklist;
	return new;
}

/* Find blacklist commands if any. */
static  int
find_blacklist(const char *modname, const struct module_blacklist *blacklist)
{
	while (blacklist) {
		if (strcmp(blacklist->modulename, modname) == 0)
			return 1;
		blacklist = blacklist->next;
	}
	return 0;
}

/* return a new alias list, with backlisted elems filtered out */
static struct module_alias *
apply_blacklist(const struct module_alias *aliases,
		const struct module_blacklist *blacklist)
{
	struct module_alias *result = NULL;
	while (aliases) {
		char *modname = aliases->module;
		if (!find_blacklist(modname, blacklist))
			result = add_alias(modname, result);
		aliases = aliases->next;
	}
	return result;
}

/* Find install commands if any. */
static const char *find_command(const char *modname,
				const struct module_command *commands)
{
	while (commands) {
		if (fnmatch(commands->modulename, modname, 0) == 0)
			return commands->command;
		commands = commands->next;
	}
	return NULL;
}

static char *append_option(char *options, const char *newoption)
{
	options = NOFAIL(realloc(options, strlen(options) + 1
				 + strlen(newoption) + 1));
	if (strlen(options)) strcat(options, " ");
	strcat(options, newoption);
	return options;
}

static char *prepend_option(char *options, const char *newoption)
{
	size_t l1, l2;
	l1 = strlen(options);
	l2 = strlen(newoption);
	/* the resulting string will look like
	 * newoption + ' ' + options + '\0' */
	if (l1) {
		options = NOFAIL(realloc(options, l2 + 1 + l1 + 1));
		memmove(options + l2 + 1, options, l1 + 1);
		options[l2] = ' ';
		memcpy(options, newoption, l2);
	} else {
		options = NOFAIL(realloc(options, l2 + 1));
		memcpy(options, newoption, l2);
		options[l2] = '\0';
	}
	return options;
}

/* Add to options */
static char *add_extra_options(const char *modname,
			       char *optstring,
			       const struct module_options *options)
{
	while (options) {
		if (strcmp(options->modulename, modname) == 0)
			optstring = prepend_option(optstring, options->options);
		options = options->next;
	}
	return optstring;
}

/* Read sysfs attribute into a buffer.
 * returns: 1 = ok, 0 = attribute missing,
 * -1 = file error (or empty file, but we don't care).
 */
static int read_attribute(const char *filename, char *buf, size_t buflen)
{
	FILE *file;
	char *s;

	file = fopen(filename, "r");
	if (file == NULL)
		return (errno == ENOENT) ? 0 : -1;
	s = fgets(buf, buflen, file);
	fclose(file);

	return (s == NULL) ? -1 : 1;
}

/* Is module in /sys/module?  If so, fill in usecount if not NULL. 
   0 means no, 1 means yes, -1 means unknown.
 */
static int module_in_kernel(const char *modname, unsigned int *usecount)
{
	int ret;
	char *name;
	struct stat finfo;

	const int ATTR_LEN = 16;
	char attr[ATTR_LEN];

	/* Check sysfs is mounted */
	if (stat("/sys/module", &finfo) < 0)
		return -1;

	/* Find module. */
	nofail_asprintf(&name, "/sys/module/%s", modname);
	ret = stat(name, &finfo);
	free(name);
	if (ret < 0)
		return (errno == ENOENT) ? 0 : -1; /* Not found or unknown. */

	/* Wait for the existing module to either go live or disappear. */
	nofail_asprintf(&name, "/sys/module/%s/initstate", modname);
	while (1) {
		ret = read_attribute(name, attr, ATTR_LEN);
		if (ret != 1 || streq(attr, "live\n"))
			break;

		usleep(100000);
	}
	free(name);

	if (ret != 1)
		return ret;

	/* Get reference count, if it exists. */
	if (usecount != NULL) {
		nofail_asprintf(&name, "/sys/module/%s/refcnt", modname);
		ret = read_attribute(name, attr, ATTR_LEN);
		free(name);
		if (ret == 1)
			*usecount = atoi(attr);
	}

	return 1;
}

/* If we don't flush, then child processes print before we do */
static void verbose_printf(int verbose, const char *fmt, ...)
{
	va_list arglist;

	if (verbose) {
		va_start(arglist, fmt);
		vprintf(fmt, arglist);
		fflush(stdout);
		va_end(arglist);
	}
}

/* Do an install/remove command: replace $CMDLINE_OPTS if it's specified. */
static void do_command(const char *modname,
		       const char *command,
		       int verbose, int dry_run,
		       errfn_t error,
		       const char *type,
		       const char *cmdline_opts)
{
	int ret;
	char *p, *replaced_cmd = NOFAIL(strdup(command));

	while ((p = strstr(replaced_cmd, "$CMDLINE_OPTS")) != NULL) {
		char *new;
		nofail_asprintf(&new, "%.*s%s%s",
			 (int)(p - replaced_cmd), replaced_cmd, cmdline_opts,
			 p + strlen("$CMDLINE_OPTS"));
		free(replaced_cmd);
		replaced_cmd = new;
	}

	verbose_printf(verbose, "%s %s\n", type, replaced_cmd);
	if (dry_run)
		return;

	setenv("MODPROBE_MODULE", modname, 1);
	ret = system(replaced_cmd);
	if (ret == -1 || WEXITSTATUS(ret))
		error("Error running %s command for %s\n", type, modname);
	free(replaced_cmd);
}

/* Actually do the insert.  Frees second arg. */
static int insmod(struct list_head *list,
		   char *optstring,
		   const char *newname,
		   int first_time,
		   errfn_t error,
		   int dry_run,
		   int verbose,
		   const struct module_options *options,
		   const struct module_command *commands,
		   int ignore_commands,
		   int ignore_proc,
		   int strip_vermagic,
		   int strip_modversion,
		   const char *cmdline_opts)
{
	int ret, fd;
	unsigned long len;
	void *map;
	const char *command;
	struct module *mod = list_entry(list->next, struct module, list);
	int rc = 0;

	/* Take us off the list. */
	list_del(&mod->list);

	/* Do things we (or parent) depend on first. */
	if (!list_empty(list)) {
		if ((rc = insmod(list, NOFAIL(strdup("")), NULL, 0, warn,
		       dry_run, verbose, options, commands, 0, ignore_proc,
		       strip_vermagic, strip_modversion, "")) != 0) {
			error("Error inserting %s (%s): %s\n",
				mod->modname, mod->filename,
				insert_moderror(errno));
			goto out_optstring;
		}
	}

	fd = open_file(mod->filename);
	if (fd < 0) {
		error("Could not open '%s': %s\n",
		      mod->filename, strerror(errno));
		goto out_optstring;
	}

	/* Don't do ANYTHING if already in kernel. */
	if (!ignore_proc
	    && module_in_kernel(newname ?: mod->modname, NULL) == 1) {
		if (first_time)
			error("Module %s already in kernel.\n",
			      newname ?: mod->modname);
		goto out_unlock;
	}

	command = find_command(mod->modname, commands);
	if (command && !ignore_commands) {
		close_file(fd);
		do_command(mod->modname, command, verbose, dry_run, error,
			   "install", cmdline_opts);
		goto out_optstring;
	}

	map = grab_fd(fd, &len);
	if (!map) {
		error("Could not read '%s': %s\n",
		      mod->filename, strerror(errno));
		goto out_unlock;
	}

	/* Rename it? */
	if (newname)
		rename_module(mod, map, len, newname);

	if (strip_modversion)
		strip_section(mod, map, len, "__versions");
	if (strip_vermagic)
		clear_magic(mod, map, len);

	/* Config file might have given more options */
	optstring = add_extra_options(mod->modname, optstring, options);

	verbose_printf(verbose, "insmod %s %s\n", mod->filename, optstring);

	if (dry_run)
		goto out;

	ret = init_module(map, len, optstring);
	if (ret != 0) {
		if (errno == EEXIST) {
			if (first_time)
				error("Module %s already in kernel.\n",
				      newname ?: mod->modname);
			goto out_unlock;
		}
		/* don't warn noisely if we're loading multiple aliases. */
		/* one of the aliases may try to use hardware we don't have. */
		if ((error != warn) || (verbose))
			error("Error inserting %s (%s): %s\n",
			      mod->modname, mod->filename,
			      insert_moderror(errno));
			rc = 1;
	}
 out:
	release_file(map, len);
 out_unlock:
	close_file(fd);
 out_optstring:
	free(optstring);
	return rc;
}

/* Do recursive removal. */
static void rmmod(struct list_head *list,
		  const char *name,
		  int first_time,
		  errfn_t error,
		  int dry_run,
		  int verbose,
		  struct module_command *commands,
		  int ignore_commands,
		  int ignore_inuse,
		  const char *cmdline_opts,
		  int flags)
{
	const char *command;
	unsigned int usecount = 0;
	struct module *mod = list_entry(list->next, struct module, list);

	/* Take first one off the list. */
	list_del(&mod->list);

	if (!name)
		name = mod->modname;

	/* Even if renamed, find commands to orig. name. */
	command = find_command(mod->modname, commands);
	if (command && !ignore_commands) {
		do_command(mod->modname, command, verbose, dry_run, error,
			   "remove", cmdline_opts);
		goto remove_rest;
	}

	if (module_in_kernel(name, &usecount) == 0)
		goto nonexistent_module;

	if (usecount != 0) {
		if (!ignore_inuse)
			error("Module %s is in use.\n", name);
		goto remove_rest;
	}

	verbose_printf(verbose, "rmmod %s\n", mod->filename);

	if (dry_run)
		goto remove_rest;

	if (delete_module(name, O_EXCL) != 0) {
		if (errno == ENOENT)
			goto nonexistent_module;
		error("Error removing %s (%s): %s\n",
		      name, mod->filename,
		      remove_moderror(errno));
	}

 remove_rest:
	/* Now do things we depend. */
	if (!list_empty(list))
		rmmod(list, NULL, 0, warn, dry_run, verbose, commands,
		      0, 1, "", flags);
	return;

nonexistent_module:
	if (first_time)
		fatal("Module %s is not in kernel.\n", mod->modname);
	goto remove_rest;
}

struct modver32_info
{
       uint32_t crc;
       char name[64 - sizeof(uint32_t)];
};

struct modver64_info
{
       uint64_t crc;
       char name[64 - sizeof(uint64_t)];
};

const char *skip_dot(const char *str)
{
       /* For our purposes, .foo matches foo.  PPC64 needs this. */
       if (str && str[0] == '.')
               return str + 1;
       return str;
}

void dump_modversions(const char *filename, errfn_t error)
{
       unsigned long size, secsize;
       void *file = grab_file(filename, &size);
       struct modver32_info *info32;
       struct modver64_info *info64;
       int n;

       if (!file) {
               error("%s: %s\n", filename, strerror(errno));
               return;
       }
       switch (elf_ident(file, size)) {
       case ELFCLASS32:
               info32 = get_section32(file, size, "__versions", &secsize);
               if (!info32)
                       return;  /* Does not seem to be a kernel module */
               if (secsize % sizeof(struct modver32_info))
                       error("Wrong section size in %s\n", filename);
               for (n = 0; n < secsize / sizeof(struct modver32_info); n++)
                       printf("0x%08lx\t%s\n", (unsigned long)
                              info32[n].crc, skip_dot(info32[n].name));
               break;

       case ELFCLASS64:
               info64 = get_section64(file, size, "__versions", &secsize);
               if (!info64)
                       return;  /* Does not seem to be a kernel module */
               if (secsize % sizeof(struct modver64_info))
                       error("Wrong section size in %s\n", filename);
               for (n = 0; n < secsize / sizeof(struct modver64_info); n++)
                       printf("0x%08llx\t%s\n", (unsigned long long)
                              info64[n].crc, skip_dot(info64[n].name));
               break;

       default:
               error("%s: ELF class not recognized\n", filename);
       }
}


/* Does path contain directory(s) subpath? */
static int type_matches(const char *path, const char *subpath)
{
	char *subpath_with_slashes;
	int ret;

	nofail_asprintf(&subpath_with_slashes, "/%s/", subpath);

	ret = (strstr(path, subpath_with_slashes) != NULL);
	free(subpath_with_slashes);
	return ret;
}

/* Careful!  Don't munge - in [ ] as per Debian Bug#350915 */
static char *underscores(char *string)
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

static int do_wildcard(const char *dirname,
		       const char *type,
		       const char *wildcard)
{
	char *modules_dep_name;
	char *line, *wcard;
	FILE *modules_dep;

	/* Canonicalize wildcard */
	wcard = strdup(wildcard);
	underscores(wcard);

	nofail_asprintf(&modules_dep_name, "%s/%s", dirname, "modules.dep");
	modules_dep = fopen(modules_dep_name, "r");
	if (!modules_dep)
		fatal("Could not load %s: %s\n",
		      modules_dep_name, strerror(errno));

	while ((line = getline_wrapped(modules_dep, NULL)) != NULL) {
		char *ptr;

		/* Ignore lines without : or which start with a # */
		ptr = strchr(line, ':');
		if (ptr == NULL || line[strspn(line, "\t ")] == '#')
			goto next;
		*ptr = '\0';

		/* "type" must match complete directory component(s). */
		if (!type || type_matches(line, type)) {
			char modname[strlen(line)+1];

			filename2modname(modname, line);
			if (fnmatch(wcard, modname, 0) == 0)
				printf("%s\n", line);
		}
	next:
		free(line);
	}

	free(modules_dep_name);
	free(wcard);
	return 0;
}

static char *strsep_skipspace(char **string, char *delim)
{
	if (!*string)
		return NULL;
	*string += strspn(*string, delim);
	return strsep(string, delim);
}

static int parse_config_scan(const char *filename,
			     const char *name,
			     int dump_only,
			     int removing,
			     struct module_options **options,
			     struct module_command **commands,
			     struct module_alias **alias,
			     struct module_blacklist **blacklist);

static int parse_config_file(const char *filename,
			    const char *name,
			    int dump_only,
			    int removing,
			    struct module_options **options,
			    struct module_command **commands,
			    struct module_alias **aliases,
			    struct module_blacklist **blacklist)
{
	char *line;
	unsigned int linenum = 0;
	FILE *cfile;

	cfile = fopen(filename, "r");
	if (!cfile)
		return 0;

	while ((line = getline_wrapped(cfile, &linenum)) != NULL) {
		char *ptr = line;
		char *cmd, *modname;

		if (dump_only)
			printf("%s\n", line);

		cmd = strsep_skipspace(&ptr, "\t ");
		if (cmd == NULL || cmd[0] == '#' || cmd[0] == '\0') {
			free(line);
			continue;
		}

		if (strcmp(cmd, "alias") == 0) {
			char *wildcard = strsep_skipspace(&ptr, "\t ");
			char *realname = strsep_skipspace(&ptr, "\t ");

			if (!wildcard || !realname)
				grammar(cmd, filename, linenum);
			else if (fnmatch(underscores(wildcard),name,0) == 0)
				*aliases = add_alias(underscores(realname), *aliases);
		} else if (strcmp(cmd, "include") == 0) {
			struct module_alias *newalias = NULL;
			char *newfilename;

			newfilename = strsep_skipspace(&ptr, "\t ");
			if (!newfilename) {
				grammar(cmd, filename, linenum);
			} else {
				warn("\"include %s\" is deprecated, "
				     "please use /etc/modprobe.d\n", newfilename);
				if (strncmp(newfilename, "/etc/modprobe.d",
					    strlen("/etc/modprobe.d")) == 0) {
					warn("\"include /etc/modprobe.d\" is "
					     "the default, ignored\n");
				} else {
					if (!parse_config_scan(newfilename, name,
							      dump_only, removing,
							      options, commands, &newalias,
							      blacklist))
						warn("Failed to open included"
						      " config file %s: %s\n",
						      newfilename, strerror(errno));
				}
				/* Files included override aliases,
				   etc that was already set ... */
				if (newalias)
					*aliases = newalias;
			}
		} else if (strcmp(cmd, "options") == 0) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname || !ptr)
				grammar(cmd, filename, linenum);
			else {
				ptr += strspn(ptr, "\t ");
				*options = add_options(underscores(modname),
						       ptr, *options);
			}
		} else if (strcmp(cmd, "install") == 0) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname || !ptr)
				grammar(cmd, filename, linenum);
			else if (!removing) {
				ptr += strspn(ptr, "\t ");
				*commands = add_command(underscores(modname),
							ptr, *commands);
			}
		} else if (strcmp(cmd, "blacklist") == 0) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname)
				grammar(cmd, filename, linenum);
			else if (!removing) {
				*blacklist = add_blacklist(underscores(modname),
							*blacklist);
			}
		} else if (strcmp(cmd, "remove") == 0) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname || !ptr)
				grammar(cmd, filename, linenum);
			else if (removing) {
				ptr += strspn(ptr, "\t ");
				*commands = add_command(underscores(modname),
							ptr, *commands);
			}
		} else if (strcmp(cmd, "config") == 0) {
			char *tmp = strsep_skipspace(&ptr, "\t ");
			if (strcmp(tmp, "binary_indexes") == 0) {
				tmp = strsep_skipspace(&ptr, "\t ");
				if (strcmp(tmp, "yes") == 0)
					use_binary_indexes = 1;
				if (strcmp(tmp, "no") == 0)
					use_binary_indexes = 0;
			}
		} else
			grammar(cmd, filename, linenum);

		free(line);
	}
	fclose(cfile);
	return 1;
}

/* fallback to plain-text aliases file as necessary */
static int read_aliases_file(const char *filename,
			     const char *name,
			     int dump_only,
			     int removing,
			     struct module_options **options,
			     struct module_command **commands,
			     struct module_alias **aliases,
			     struct module_blacklist **blacklist)
{
	struct index_value *realnames;
	struct index_value *realname;
	char *binfile;
	struct index_file *index;

	if (!use_binary_indexes)
		goto plain_text;

	nofail_asprintf(&binfile, "%s.bin", filename);
	index = index_file_open(binfile);
	if (!index) {
		free(binfile);
		goto plain_text;
	}

	if (dump_only) {
		index_dump(index, stdout, "alias ");
		free(binfile);
		index_file_close(index);
		return 1;
	}

	realnames = index_searchwild(index, name);
	for (realname = realnames; realname; realname = realname->next)
		*aliases = add_alias(realname->value, *aliases);
	index_values_free(realnames);

	free(binfile);
	index_file_close(index);
	return 1;

plain_text:
	return parse_config_file(filename, name, dump_only, removing,
				 options, commands, aliases, blacklist);
}

static int parse_config_scan(const char *filename,
			     const char *name,
			     int dump_only,
			     int removing,
			     struct module_options **options,
			     struct module_command **commands,
			     struct module_alias **aliases,
			     struct module_blacklist **blacklist)
{
	DIR *dir;
	int ret = 0;

	dir = opendir(filename);
	if (dir) {
		struct file_entry {
			struct list_head node;
			char name[];
		};
		LIST_HEAD(files_list);
		struct file_entry *fe, *fe_tmp;
		struct dirent *i;

		/* sort files from directory into list */
		while ((i = readdir(dir)) != NULL) {
			size_t len;

			if (i->d_name[0] == '.')
				continue;
			if (!config_filter(i->d_name))
				continue;

			len = strlen(i->d_name);
			if (len < 6 ||
			    (strcmp(&i->d_name[len-5], ".conf") != 0 &&
			     strcmp(&i->d_name[len-6], ".alias") != 0))
				warn("All config files need .conf: %s/%s, "
				     "it will be ignored in a future release.\n",
				     filename, i->d_name);
			fe = malloc(sizeof(struct file_entry) + len + 1);
			if (fe == NULL)
				continue;
			strcpy(fe->name, i->d_name);
			list_for_each_entry(fe_tmp, &files_list, node)
				if (strcmp(fe_tmp->name, fe->name) >= 0)
					break;
			list_add_tail(&fe->node, &fe_tmp->node);
		}
		closedir(dir);

		/* parse list of files */
		list_for_each_entry_safe(fe, fe_tmp, &files_list, node) {
			char *cfgfile;

			nofail_asprintf(&cfgfile, "%s/%s", filename, fe->name);
			if (!parse_config_file(cfgfile, name,
					       dump_only, removing,
					       options, commands,
					       aliases, blacklist))
				warn("Failed to open config file "
				     "%s: %s\n", fe->name, strerror(errno));
			free(cfgfile);
			list_del(&fe->node);
			free(fe);
		}

		ret = 1;
	} else {
		if (parse_config_file(filename, name, dump_only, removing,
				      options, commands, aliases, blacklist))
			ret = 1;
	}
	return ret;
}

/* Read binary index file containing aliases only */
static void parse_toplevel_config(const char *filename,
				  const char *name,
				  int dump_only,
				  int removing,
				  struct module_options **options,
				  struct module_command **commands,
				  struct module_alias **aliases,
				  struct module_blacklist **blacklist)
{
	if (filename) {
		if (!parse_config_scan(filename, name, dump_only, removing,
				       options, commands, aliases, blacklist))
			fatal("Failed to open config file %s: %s\n",
			      filename, strerror(errno));
		return;
	}

	/* deprecated config file */
	if (parse_config_file("/etc/modprobe.conf", name, dump_only, removing,
			      options, commands, aliases, blacklist) > 0)
		warn("Deprecated config file /etc/modprobe.conf, "
		      "all config files belong into /etc/modprobe.d/.\n");

	/* default config */
	parse_config_scan("/etc/modprobe.d", name, dump_only, removing,
			  options, commands, aliases, blacklist);
}

/* Read possible module arguments from the kernel command line. */
static int parse_kcmdline(int dump_only, struct module_options **options)
{
	char *line;
	unsigned int linenum = 0;
	FILE *kcmdline;

	kcmdline = fopen("/proc/cmdline", "r");
	if (!kcmdline)
		return 0;

	while ((line = getline_wrapped(kcmdline, &linenum)) != NULL) {
		char *ptr = line;
		char *arg;

		while ((arg = strsep_skipspace(&ptr, "\t ")) != NULL) {
			char *sep, *modname, *opt;

			sep = strchr(arg, '.');
			if (sep) {
				if (!strchr(sep, '='))
					continue;
				modname = arg;
				*sep = '\0';
				opt = ++sep;

				if (dump_only)
					printf("options %s %s\n", modname, opt);

				*options = add_options(underscores(modname),
						       opt, *options);
			}
		}

		free(line);
	}
	fclose(kcmdline);
	return 1;
}

static void add_to_env_var(const char *option)
{
	const char *oldenv;

	if ((oldenv = getenv("MODPROBE_OPTIONS")) != NULL) {
		char *newenv;
		nofail_asprintf(&newenv, "%s %s", oldenv, option);
		setenv("MODPROBE_OPTIONS", newenv, 1);
	} else
		setenv("MODPROBE_OPTIONS", option, 1);
}

/* Prepend options from environment. */
static char **merge_args(char *args, char *argv[], int *argc)
{
	char *arg, *argstring;
	char **newargs = NULL;
	unsigned int i, num_env = 0;

	if (!args)
		return argv;

	argstring = NOFAIL(strdup(args));
	for (arg = strtok(argstring, " "); arg; arg = strtok(NULL, " ")) {
		num_env++;
		newargs = NOFAIL(realloc(newargs,
					 sizeof(newargs[0])
					 * (num_env + *argc + 1)));
		newargs[num_env] = arg;
	}

	if (!newargs)
		return argv;

	/* Append commandline args */
	newargs[0] = argv[0];
	for (i = 1; i <= *argc; i++)
		newargs[num_env+i] = argv[i];

	*argc += num_env;
	return newargs;
}

static char *gather_options(char *argv[])
{
	char *optstring = NOFAIL(strdup(""));

	/* Rest is module options */
	while (*argv) {
		/* Quote value if it contains spaces. */
		unsigned int eq = strcspn(*argv, "=");

		if (strchr(*argv+eq, ' ') && !strchr(*argv, '"')) {
			char quoted[strlen(*argv) + 3];
			(*argv)[eq] = '\0';
			sprintf(quoted, "%s=\"%s\"", *argv, *argv+eq+1);
			optstring = append_option(optstring, quoted);
		} else
			optstring = append_option(optstring, *argv);
		argv++;
	}
	return optstring;
}

static int handle_module(const char *modname,
			  struct list_head *todo_list,
			  const char *newname,
			  int remove,
			  char *options,
			  int first_time,
			  errfn_t error,
			  int dry_run,
			  int verbose,
			  struct module_options *modoptions,
			  struct module_command *commands,
			  int ignore_commands,
			  int ignore_proc,
			  int strip_vermagic,
			  int strip_modversion,
			  const char *cmdline_opts,
			  int flags)
{
	if (list_empty(todo_list)) {
		const char *command;

		/* The dependencies have to be real modules, but
		   handle case where the first is completely bogus. */
		command = find_command(modname, commands);
		if (command && !ignore_commands) {
			do_command(modname, command, verbose, dry_run, error,
				   remove ? "remove":"install", cmdline_opts);
			return 0;
		}

		if (!quiet)
			error("Module %s not found.\n", modname);
		return 1;
	}

	if (remove)
		rmmod(todo_list, newname, first_time, error, dry_run, verbose,
		      commands, ignore_commands, 0, cmdline_opts, flags);
	else
		insmod(todo_list, NOFAIL(strdup(options)), newname,
		       first_time, error, dry_run, verbose, modoptions,
		       commands, ignore_commands, ignore_proc, strip_vermagic,
		       strip_modversion, cmdline_opts);

	return 0;
}

static struct option options[] = { { "verbose", 0, NULL, 'v' },
				   { "version", 0, NULL, 'V' },
				   { "config", 1, NULL, 'C' },
				   { "name", 1, NULL, 'o' },
				   { "remove", 0, NULL, 'r' },
				   { "wait", 0, NULL, 'w' },
				   { "showconfig", 0, NULL, 'c' },
				   { "quiet", 0, NULL, 'q' },
				   { "show", 0, NULL, 'n' },
				   { "dry-run", 0, NULL, 'n' },
				   { "syslog", 0, NULL, 's' },
				   { "type", 1, NULL, 't' },
				   { "list", 0, NULL, 'l' },
				   { "all", 0, NULL, 'a' },
				   { "ignore-install", 0, NULL, 'i' },
				   { "ignore-remove", 0, NULL, 'i' },
				   { "force", 0, NULL, 'f' },
				   { "force-vermagic", 0, NULL, 1 },
				   { "force-modversion", 0, NULL, 2 },
				   { "set-version", 1, NULL, 'S' },
				   { "show-depends", 0, NULL, 'D' },
				   { "dirname", 1, NULL, 'd' },
				   { "first-time", 0, NULL, 3 },
				   { "dump-modversions", 0, NULL, 4 },
				   { "use-blacklist", 0, NULL, 'b' },
				   { NULL, 0, NULL, 0 } };

int main(int argc, char *argv[])
{
	struct utsname buf;
	struct stat statbuf;
	int opt;
	int dump_only = 0;
	int dry_run = 0;
	int remove = 0;
	int verbose = 0;
	int list_only = 0;
	int all = 0;
	int ignore_commands = 0;
	int strip_vermagic = 0;
	int strip_modversion = 0;
	int ignore_proc = 0;
	int first_time = 0;
	int dump_modver = 0;
	int use_blacklist = 0;
	unsigned int i, num_modules;
	char *type = NULL;
	const char *config = NULL;
	char *dirname = NULL;
	char *optstring = NULL;
	char *newname = NULL;
	char *aliasfilename, *symfilename;
	errfn_t error = fatal;
	int flags = O_NONBLOCK|O_EXCL;
	int was_error = 0;

	/* Prepend options from environment. */
	argv = merge_args(getenv("MODPROBE_OPTIONS"), argv, &argc);

	uname(&buf);
	while ((opt = getopt_long(argc, argv, "vVC:o:rnqQsclt:aifbwd:", options, NULL)) != -1){
		switch (opt) {
		case 'v':
			add_to_env_var("-v");
			verbose = 1;
			break;
		case 'V':
			puts(PACKAGE " version " VERSION);
			exit(0);
		case 'S':
			strncpy(buf.release, optarg, sizeof(buf.release));
			buf.release[sizeof(buf.release)-1] = '\0';
			break;
		case 'C':
			config = optarg;
			add_to_env_var("-C");
			add_to_env_var(config);
			break;
		case 'q':
			quiet = 1;
			add_to_env_var("-q");
			break;
		case 'D':
			dry_run = 1;
			ignore_proc = 1;
			verbose = 1;
			add_to_env_var("-D");
			break;
		case 'o':
			newname = optarg;
			break;
		case 'r':
			remove = 1;
			break;
		case 'c':
			dump_only = 1;
			break;
		case 't':
			type = optarg;
			break;
		case 'l':
			list_only = 1;
			break;
		case 'a':
			all = 1;
			error = warn;
			break;
		case 'n':
			dry_run = 1;
			break;
		case 's':
			add_to_env_var("-s");
			logging = 1;
			break;
		case 'i':
			ignore_commands = 1;
			break;
		case 'f':
			strip_vermagic = 1;
			strip_modversion = 1;
			break;
		case 'b':
			use_blacklist = 1;
			break;
		case 'w':
			flags &= ~O_NONBLOCK;
			break;
		case 'd':
			nofail_asprintf(&dirname, "%s/%s/%s", optarg,
					MODULE_DIR, buf.release);
			break;
		case 1:
			strip_vermagic = 1;
			break;
		case 2:
			strip_modversion = 1;
			break;
		case 3:
			first_time = 1;
			break;
		case 4:
			dump_modver = 1;
			break;
		default:
			print_usage(argv[0]);
		}
	}

	/* If stderr not open, go to syslog */
	if (logging || fstat(STDERR_FILENO, &statbuf) != 0) {
		openlog("modprobe", LOG_CONS, LOG_DAEMON);
		logging = 1;
	}

	if (argc < optind + 1 && !dump_only && !list_only && !remove)
		print_usage(argv[0]);

	if (!dirname)
		nofail_asprintf(&dirname, "%s/%s", MODULE_DIR, buf.release);
	nofail_asprintf(&aliasfilename, "%s/modules.alias", dirname);
	nofail_asprintf(&symfilename, "%s/modules.symbols", dirname);

	/* Old-style -t xxx wildcard?  Only with -l. */
	if (list_only) {
		if (optind+1 < argc)
			fatal("Can't have multiple wildcards\n");
		/* fprintf(stderr, "man find\n"); return 1; */
		return do_wildcard(dirname, type, argv[optind]?:"*");
	}
	if (type)
		fatal("-t only supported with -l");

	if (dump_only) {
		struct module_command *commands = NULL;
		struct module_options *modoptions = NULL;
		struct module_alias *aliases = NULL;
		struct module_blacklist *blacklist = NULL;

		parse_toplevel_config(config, "", 1, 0, &modoptions,
				      &commands, &aliases, &blacklist);
		/* Read module options from kernel command line */
		parse_kcmdline(1, &modoptions);
		parse_config_file(aliasfilename, "", 1, 0, &modoptions,
				  &commands, &aliases, &blacklist);
		parse_config_file(symfilename, "", 1, 0, &modoptions,
				  &commands, &aliases, &blacklist);
		exit(0);
	}

	if (remove || all) {
		num_modules = argc - optind;
		optstring = NOFAIL(strdup(""));
	} else {
		num_modules = 1;
		optstring = gather_options(argv+optind+1);
	}

	/* num_modules is always 1 except for -r or -a. */
	for (i = 0; i < num_modules; i++) {
		struct module_command *commands = NULL;
		struct module_options *modoptions = NULL;
		struct module_alias *aliases = NULL;
		struct module_blacklist *blacklist = NULL;
		LIST_HEAD(list);
		char *modulearg = argv[optind + i];

		if (dump_modver) {
			dump_modversions(modulearg, error);
			continue;
		}

		/* Convert name we are looking for */
		underscores(modulearg);

		/* Returns the resolved alias, options */
		parse_toplevel_config(config, modulearg, 0,
		     remove, &modoptions, &commands, &aliases, &blacklist);

		/* Read module options from kernel command line */
		parse_kcmdline(0, &modoptions);

		/* No luck?  Try symbol names, if starts with symbol:. */
		if (!aliases &&
		    strncmp(modulearg, "symbol:", strlen("symbol:")) == 0) {
			parse_config_file(symfilename, modulearg, 0,
					  remove, &modoptions, &commands,
					  &aliases, &blacklist);
		}
		if (!aliases) {
			if(!strchr(modulearg, ':'))
				read_depends(dirname, modulearg, &list);

			/* We only use canned aliases as last resort. */
			if (list_empty(&list)
			    && !find_command(modulearg, commands))
			{
				read_aliases_file(aliasfilename,
						  modulearg, 0, remove,
						  &modoptions, &commands,
						  &aliases, &blacklist);
			}
		}

		aliases = apply_blacklist(aliases, blacklist);
		if (aliases) {
			errfn_t err = error;

			/* More than one alias?  Don't bail out on failure. */
			if (aliases->next)
				err = warn;
			while (aliases) {
				/* Add the options for this alias. */
				char *opts = NOFAIL(strdup(optstring));
				opts = add_extra_options(modulearg,
							 opts, modoptions);

				read_depends(dirname, aliases->module, &list);
				if (handle_module(aliases->module, &list,
					      newname, remove, opts,
					      first_time, err,
					      dry_run, verbose, modoptions,
					      commands, ignore_commands,
					      ignore_proc, strip_vermagic,
					      strip_modversion,
					      optstring, flags))
					was_error = 1;

				aliases = aliases->next;
				INIT_LIST_HEAD(&list);
			}
		} else {
			if (use_blacklist
			    && find_blacklist(modulearg, blacklist))
				continue;

			if (handle_module(modulearg, &list, newname, remove,
				      optstring, first_time, error, dry_run,
				      verbose, modoptions, commands,
				      ignore_commands, ignore_proc,
				      strip_vermagic, strip_modversion,
				      optstring, flags))
				was_error = 1;
		}
	}
	if (logging)
		closelog();

	free(dirname);
	free(aliasfilename);
	free(symfilename);
	free(optstring);

	if (was_error)
		exit(1);
	else
		exit(0);
}
