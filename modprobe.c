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

#include "util.h"
#include "elfops.h"
#include "zlibsupport.h"
#include "logging.h"
#include "index.h"
#include "list.h"
#include "config_filter.h"

#include "testing.h"

int use_binary_indexes = 1; /* default to enabled. */

/* Limit do_softdep/do_modprobe recursion.
 * This is a simple way to handle dependency loops
 * caused by poorly written softdep commands.
 */
static int recursion_depth = 0;
const int MAX_RECURSION = 50; /* Arbitrary choice */

extern long init_module(void *, unsigned long, const char *);
extern long delete_module(const char *, unsigned int);

struct module {
	struct list_head list;
	char *modname;
	char filename[0];
};

typedef enum
{
	mit_remove = 1,
	mit_dry_run = 2,
	mit_first_time = 4,
	mit_use_blacklist = 8,
	mit_ignore_commands = 16,
	mit_ignore_loaded = 32,
	mit_strip_vermagic = 64,
	mit_strip_modversion = 128,
	mit_resolve_alias = 256

} modprobe_flags_t;

#ifndef MODULE_DIR
#define MODULE_DIR "/lib/modules"
#endif

static void print_usage(const char *progname)
{
	fprintf(stderr,
		"Usage: %s [-v] [-V] [-C config-file] [-d <dirname> ] [-n] [-i] [-q] [-b] [-o <modname>] [ --dump-modversions ] <modname> [parameters...]\n"
		"%s -r [-n] [-i] [-v] <modulename> ...\n"
		"%s -l -t <dirname> [ -a <modulename> ...]\n",
		progname, progname, progname);
	exit(1);
}

static struct module *find_module(const char *filename, struct list_head *list)
{
	struct module *i;

	list_for_each_entry(i, list, list) {
		if (streq(i->filename, filename))
			return i;
	}
	return NULL;
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

static void free_module(struct module *mod)
{
	free(mod->modname);
	free(mod);
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
	modname = my_basename(line);

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

static void replace_modname(struct elf_file *module,
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

	warn("Could not find old name in %s to replace!\n", module->pathname);
}

static void rename_module(struct elf_file *module,
			  const char *oldname,
			  const char *newname)
{
	void *modstruct;
	unsigned long len;

	/* Old-style */
	modstruct = module->ops->load_section(module,
		".gnu.linkonce.this_module", &len);
	/* New-style */
	if (!modstruct)
		modstruct = module->ops->load_section(module, "__module", &len);
	if (!modstruct)
		warn("Could not find module name to change in %s\n",
		     module->pathname);
	else
		replace_modname(module, modstruct, len, oldname, newname);
}

static void clear_magic(struct elf_file *module)
{
	struct string_table *tbl;
	int j;

	/* Old-style: __vermagic section */
	module->ops->strip_section(module, "__vermagic");

	/* New-style: in .modinfo section */
	tbl = module->ops->load_strings(module, ".modinfo", NULL);
	for (j = 0; tbl && j < tbl->cnt; j++) {
		const char *p = tbl->str[j];
		if (strstarts(p, "vermagic=")) {
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

struct module_softdep
{
	struct module_softdep *next;
	char *buf;
	/* The modname and string tables point to buf. */
	char *modname;
	struct string_table *pre;
	struct string_table *post;
};

struct modprobe_conf
{
	struct module_options *options;
	struct module_command *commands;
	struct module_alias *aliases;
	struct module_blacklist *blacklist;
	struct module_softdep *softdeps;
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

static void free_aliases(struct module_alias *alias_list)
{
	while (alias_list) {
		struct module_alias *alias;

		alias = alias_list;
		alias_list = alias_list->next;

		free(alias->module);
		free(alias);
	}
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
		if (streq(blacklist->modulename, modname))
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

/* Find soft dependencies, if any. */
static const struct module_softdep *
find_softdep(const char *modname, const struct module_softdep *softdeps)
{
	while (softdeps) {
		if (fnmatch(softdeps->modname, modname, 0) == 0)
			return softdeps;
		softdeps = softdeps->next;
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
		if (streq(options->modulename, modname))
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

/* is this a built-in module?
 * 0: no, 1: yes, -1: don't know
 */
static int module_builtin(const char *dirname, const char *modname)
{
	struct index_file *index;
	char *filename, *value;

	nofail_asprintf(&filename, "%s/modules.builtin.bin", dirname);
	index = index_file_open(filename);
	free(filename);
	if (!index)
		return -1;
	value = index_search(index, modname);
	free(value);
	return value ? 1 : 0;
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

void dump_modversions(const char *filename, errfn_t error)
{
	struct elf_file *module;

	module = grab_elf_file(filename);
	if (!module) {
		error("%s: %s\n", filename, strerror(errno));
		return;
	}
	if (module->ops->dump_modvers(module) < 0)
		error("Wrong section size in '%s'\n", filename);
	release_elf_file(module);
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
			     struct modprobe_conf *conf,
			     int dump_only,
			     int removing);

static int parse_config_file(const char *filename,
			    const char *name,
			    struct modprobe_conf *conf,
			    int dump_only,
			    int removing)
{
	char *line;
	unsigned int linenum = 0;
	FILE *cfile;

	struct module_options **options = &conf->options;
	struct module_command **commands = &conf->commands;
	struct module_alias **aliases = &conf->aliases;
	struct module_blacklist **blacklist = &conf->blacklist;

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

		if (streq(cmd, "alias")) {
			char *wildcard = strsep_skipspace(&ptr, "\t ");
			char *realname = strsep_skipspace(&ptr, "\t ");
			if (!wildcard || !realname)
				goto syntax_error;
			if (fnmatch(underscores(wildcard),name,0) == 0)
				*aliases = add_alias(underscores(realname), *aliases);
		} else if (streq(cmd, "include")) {
			struct modprobe_conf newconf = *conf;
			newconf.aliases = NULL;
			char *newfilename;
			newfilename = strsep_skipspace(&ptr, "\t ");
			if (!newfilename)
				goto syntax_error;

			warn("\"include %s\" is deprecated, "
			     "please use /etc/modprobe.d\n", newfilename);
			if (strstarts(newfilename, "/etc/modprobe.d")) {
				warn("\"include /etc/modprobe.d\" is "
				     "the default, ignored\n");
			} else {
				if (!parse_config_scan(newfilename, name,
						       &newconf, dump_only,
						       removing))
					warn("Failed to open included"
					      " config file %s: %s\n",
					      newfilename, strerror(errno));
			}
			/* Files included override aliases,
			   etc that was already set ... */
			if (newconf.aliases)
				*aliases = newconf.aliases;

		} else if (streq(cmd, "options")) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname || !ptr)
				goto syntax_error;

			ptr += strspn(ptr, "\t ");
			*options = add_options(underscores(modname),
					       ptr, *options);

		} else if (streq(cmd, "install")) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname || !ptr)
				goto syntax_error;
			if (!removing) {
				ptr += strspn(ptr, "\t ");
				*commands = add_command(underscores(modname),
							ptr, *commands);
			}
		} else if (streq(cmd, "blacklist")) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname)
				goto syntax_error;
			if (!removing) {
				*blacklist = add_blacklist(underscores(modname),
							*blacklist);
			}
		} else if (streq(cmd, "remove")) {
			modname = strsep_skipspace(&ptr, "\t ");
			if (!modname || !ptr)
				goto syntax_error;
			if (removing) {
				ptr += strspn(ptr, "\t ");
				*commands = add_command(underscores(modname),
							ptr, *commands);
			}
		} else if (streq(cmd, "softdep")) {
			char *tk;
			int pre = 0, post = 0;
			struct string_table *pre_modnames = NULL;
			struct string_table *post_modnames = NULL;
			struct module_softdep *new;

			modname = underscores(strsep_skipspace(&ptr, "\t "));
			if (!modname || !ptr)
				goto syntax_error;
			while ((tk = strsep_skipspace(&ptr, "\t ")) != NULL) {
				if (streq(tk, "pre:")) {
					pre = 1; post = 0;
				} else if (streq(tk, "post:")) {
					pre = 0; post = 1;
				} else if (pre) {
					pre_modnames = NOFAIL(
						strtbl_add(tk, pre_modnames));
				} else if (post) {
					post_modnames = NOFAIL(
						strtbl_add(tk, post_modnames));
				} else {
					strtbl_free(pre_modnames);
					strtbl_free(post_modnames);
					goto syntax_error;
				}
			}
			new = NOFAIL(malloc(sizeof(*new)));
			new->buf = line;
			new->modname = modname;
			new->pre = pre_modnames;
			new->post = post_modnames;
			new->next = conf->softdeps;
			conf->softdeps = new;

			line = NULL; /* Don't free() this line. */

		} else if (streq(cmd, "config")) {
			char *tmp = strsep_skipspace(&ptr, "\t ");

			if (!tmp)
				goto syntax_error;
			if (streq(tmp, "binary_indexes")) {
				tmp = strsep_skipspace(&ptr, "\t ");
				if (streq(tmp, "yes"))
					use_binary_indexes = 1;
				if (streq(tmp, "no"))
					use_binary_indexes = 0;
			}
		} else {
syntax_error:
			grammar(cmd, filename, linenum);
		}

		free(line);
	}
	fclose(cfile);
	return 1;
}

/* Read binary index file containing aliases only */
static int read_aliases_file(const char *filename,
			     const char *name,
			     int dump_only,
			     struct module_alias **aliases)
{
	struct index_value *realnames;
	struct index_value *realname;
	char *binfile;
	struct index_file *index;

	nofail_asprintf(&binfile, "%s.bin", filename);
	index = index_file_open(binfile);
	if (!index) {
		free(binfile);
		return 0;
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
}

/* fallback to plain-text aliases file if necessary */
static int read_aliases(const char *filename,
			const char *name,
			int dump_only,
			struct module_alias **aliases)
{
	struct modprobe_conf conf = { .aliases = *aliases };
	int ret;

	if (use_binary_indexes)
		if (read_aliases_file(filename, name, dump_only, aliases))
			return 1;

	ret = parse_config_file(filename, name, &conf, dump_only, 0);
	*aliases = conf.aliases;
	return ret;
}

static int parse_config_scan(const char *filename,
			     const char *name,
			     struct modprobe_conf *conf,
			     int dump_only,
			     int removing)
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
			if (!parse_config_file(cfgfile, name, conf,
					       dump_only, removing))
				warn("Failed to open config file "
				     "%s: %s\n", fe->name, strerror(errno));
			free(cfgfile);
			list_del(&fe->node);
			free(fe);
		}

		ret = 1;
	} else {
		if (parse_config_file(filename, name, conf, dump_only, removing))
			ret = 1;
	}
	return ret;
}

static void parse_toplevel_config(const char *filename,
				  const char *name,
				  struct modprobe_conf *conf,
				  int dump_only,
				  int removing)
{
	if (filename) {
		if (!parse_config_scan(filename, name, conf, dump_only, removing))
			fatal("Failed to open config file %s: %s\n",
			      filename, strerror(errno));
		return;
	}

	/* deprecated config file */
	if (parse_config_file("/etc/modprobe.conf", name, conf,
			      dump_only, removing) > 0)
		warn("Deprecated config file /etc/modprobe.conf, "
		      "all config files belong into /etc/modprobe.d/.\n");

	/* default config */
	parse_config_scan("/etc/modprobe.d", name, conf, dump_only, removing);
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
		free(newenv);
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

/* Do an install/remove command: replace $CMDLINE_OPTS if it's specified. */
static void do_command(const char *modname,
		       const char *command,
		       int dry_run,
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

	info("%s %s\n", type, replaced_cmd);
	if (dry_run)
		goto out;

	setenv("MODPROBE_MODULE", modname, 1);
	ret = system(replaced_cmd);
	if (ret == -1 || WEXITSTATUS(ret))
		error("Error running %s command for %s\n", type, modname);

out:
	free(replaced_cmd);
}

/* Forward declaration */
int do_modprobe(const char *modname,
		const char *newname,
		const char *cmdline_opts,
		const char *configname,
		const char *dirname,
		errfn_t error,
		modprobe_flags_t flags);

static void do_softdep(const struct module_softdep *softdep,
		       const char *cmdline_opts,
		       const char *configname,
		       const char *dirname,
		       errfn_t error,
		       modprobe_flags_t flags)
{
	struct string_table *pre_modnames, *post_modnames;
	int i, j;

	if (++recursion_depth >= MAX_RECURSION)
		fatal("modprobe: softdep dependency loop encountered %s %s\n",
			(flags & mit_remove) ? "removing" : "inserting",
			softdep->modname);

	if (flags & mit_remove) {
		/* Reverse module order if removing. */
		pre_modnames = softdep->post;
		post_modnames = softdep->pre;
	} else {
		pre_modnames = softdep->pre;
		post_modnames = softdep->post;
	}

	/* Modprobe pre_modnames */

	for (i = 0; pre_modnames && i < pre_modnames->cnt; i++) {
		/* Reverse module order if removing. */
		j = (flags & mit_remove) ? pre_modnames->cnt-1 - i : i;

		do_modprobe(pre_modnames->str[j], NULL, "",
			configname, dirname, warn, flags);
	}

	/* Modprobe main module, passing cmdline_opts, ignoring softdep */

	do_modprobe(softdep->modname, NULL, cmdline_opts,
		configname, dirname, warn, flags | mit_ignore_commands);

	/* Modprobe post_modnames */

	for (i = 0; post_modnames && i < post_modnames->cnt; i++) {
		/* Reverse module order if removing. */
		j = (flags & mit_remove) ? post_modnames->cnt-1 - i : i;

		do_modprobe(post_modnames->str[j], NULL, "", configname,
			dirname, warn, flags);
	}
}

/* Actually do the insert.  Frees second arg. */
static int insmod(struct list_head *list,
		   char *optstring,
		   const char *newname,
		   const struct module_options *options,
		   const struct module_softdep *softdeps,
		   const struct module_command *commands,
		   const char *cmdline_opts,
		   const char *configname,
		   const char *dirname,
		   errfn_t error,
		   modprobe_flags_t flags)
{
	int ret, fd;
	struct elf_file *module;
	const struct module_softdep *softdep;
	const char *command;
	struct module *mod = list_entry(list->next, struct module, list);
	int rc = 0;

	/* Take us off the list. */
	list_del(&mod->list);

	/* Do things we (or parent) depend on first. */
	if (!list_empty(list)) {
		modprobe_flags_t f = flags;
		f &= ~mit_first_time;
		f &= ~mit_ignore_commands;
		if ((rc = insmod(list, NOFAIL(strdup("")), NULL,
		       options, softdeps, commands, "",
		       configname, dirname, warn, f)) != 0)
		{
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
	if (!(flags & mit_ignore_loaded)
	    && module_in_kernel(newname ?: mod->modname, NULL) == 1) {
		if (flags & mit_first_time)
			error("Module %s already in kernel.\n",
			      newname ?: mod->modname);
		goto out_unlock;
	}

	softdep = find_softdep(mod->modname, softdeps);
	if (softdep && !(flags & mit_ignore_commands)) {
		close_file(fd);
		do_softdep(softdep, cmdline_opts, configname, dirname, 
			   error, flags & (mit_remove | mit_dry_run));
		goto out_optstring;
	}

	command = find_command(mod->modname, commands);
	if (command && !(flags & mit_ignore_commands)) {
		close_file(fd);
		do_command(mod->modname, command, flags & mit_dry_run, error,
			   "install", cmdline_opts);
		goto out_optstring;
	}

	module = grab_elf_file_fd(mod->filename, fd);
	if (!module) {
		error("Could not read '%s': %s\n", mod->filename,
			(errno == ENOEXEC) ? "Invalid module format" :
				strerror(errno));
		goto out_unlock;
	}
	if (newname)
		rename_module(module, mod->modname, newname);
	if (flags & mit_strip_modversion)
		module->ops->strip_section(module, "__versions");
	if (flags & mit_strip_vermagic)
		clear_magic(module);

	/* Config file might have given more options */
	optstring = add_extra_options(mod->modname, optstring, options);

	info("insmod %s %s\n", mod->filename, optstring);

	if (flags & mit_dry_run)
		goto out;

	ret = init_module(module->data, module->len, optstring);
	if (ret != 0) {
		if (errno == EEXIST) {
			if (flags & mit_first_time)
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
	release_elf_file(module);
 out_unlock:
	close_file(fd);
 out_optstring:
	free(optstring);
	free_module(mod);
	return rc;
}

/* Do recursive removal. */
static void rmmod(struct list_head *list,
		  const char *name,
		  struct module_softdep *softdeps,
		  struct module_command *commands,
		  const char *cmdline_opts,
		  const char *configname,
		  const char *dirname,
		  errfn_t error,
		  modprobe_flags_t flags)
{
	const struct module_softdep *softdep;
	const char *command;
	unsigned int usecount = 0;
	struct module *mod = list_entry(list->next, struct module, list);

	/* Take first one off the list. */
	list_del(&mod->list);

	if (!name)
		name = mod->modname;

	/* Even if renamed, find commands/softdeps to orig. name. */

	softdep = find_softdep(mod->modname, softdeps);
	if (softdep && !(flags & mit_ignore_commands)) {
		do_softdep(softdep, cmdline_opts, configname, dirname,
			   error, flags & (mit_remove | mit_dry_run));
		goto remove_rest;
	}

	command = find_command(mod->modname, commands);
	if (command && !(flags & mit_ignore_commands)) {
		do_command(mod->modname, command, flags & mit_dry_run, error,
			   "remove", cmdline_opts);
		goto remove_rest;
	}

	if (module_in_kernel(name, &usecount) == 0)
		goto nonexistent_module;

	if (usecount != 0) {
		if (!(flags & mit_ignore_loaded))
			error("Module %s is in use.\n", name);
		goto remove_rest;
	}

	info("rmmod %s\n", mod->filename);

	if (flags & mit_dry_run)
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
	if (!list_empty(list)) {
		flags &= ~mit_first_time;
		flags &= ~mit_ignore_commands;
		flags |= mit_ignore_loaded;

		rmmod(list, NULL, softdeps, commands, "",
		      configname, dirname, warn, flags);
	}
	free_module(mod);
	return;

nonexistent_module:
	if (flags & mit_first_time)
		fatal("Module %s is not in kernel.\n", mod->modname);
	goto remove_rest;
}

static int handle_module(const char *modname,
			  struct list_head *todo_list,
			  const char *newname,
			  const char *options,
			  struct module_options *modoptions,
			  struct module_softdep *softdeps,
			  struct module_command *commands,
			  const char *cmdline_opts,
			  const char *configname,
			  const char *dirname,
			  errfn_t error,
			  modprobe_flags_t flags)
{
	if (list_empty(todo_list)) {
		const struct module_softdep *softdep;
		const char *command;

		/* The dependencies have to be real modules, but
		   handle case where the first is completely bogus. */

		softdep = find_softdep(modname, softdeps);
		if (softdep && !(flags & mit_ignore_commands)) {
			do_softdep(softdep, cmdline_opts, configname, dirname,
				   error, flags & (mit_remove | mit_dry_run));
			return 0;
		}

		command = find_command(modname, commands);
		if (command && !(flags & mit_ignore_commands)) {
			do_command(modname, command, flags & mit_dry_run, error,
				   (flags & mit_remove) ? "remove":"install", cmdline_opts);
			return 0;
		}

		if (!quiet)
			error("Module %s not found.\n", modname);
		return 1;
	}

	if (flags & mit_remove) {
		flags &= ~mit_ignore_loaded;
		rmmod(todo_list, newname, softdeps, commands, cmdline_opts,
		      configname, dirname, error, flags);
	} else
		insmod(todo_list, NOFAIL(strdup(options)), newname,
		       modoptions, softdeps, commands, cmdline_opts,
		       configname, dirname, error, flags);

	return 0;
}

int handle_builtin_module(const char *modname,
                          errfn_t error,
                          modprobe_flags_t flags)
{
	if (flags & mit_remove) {
		error("Module %s is builtin\n", modname);
		return 1;
	} else if (flags & mit_first_time) {
		error("Module %s already in kernel (builtin).\n", modname);
		return 1;
	} else if (flags & mit_ignore_loaded) {
		/* --show-depends given */
		info("builtin %s\n", modname);
	}
	return 0;
}

int do_modprobe(const char *modulename,
		const char *newname,
		const char *cmdline_opts,
		const char *configname,
		const char *dirname,
		errfn_t error,
		modprobe_flags_t flags)
{
	char *modname;
	struct modprobe_conf conf = {};
	struct module_alias *filtered_aliases;
	LIST_HEAD(list);
	int failed = 0;

	/* Convert name we are looking for */
	modname = underscores(NOFAIL(strdup(modulename)));

	/* Returns the resolved alias, options */
	parse_toplevel_config(configname, modname, &conf, 0, flags & mit_remove);

	/* Read module options from kernel command line */
	parse_kcmdline(0, &conf.options);

	/* No luck?  Try symbol names, if starts with symbol:. */
	if (!conf.aliases && strstarts(modname, "symbol:")) {
		char *symfilename;

		nofail_asprintf(&symfilename, "%s/modules.symbols", dirname);
		read_aliases(symfilename, modname, 0, &conf.aliases);
		free(symfilename);
	}
	if (!conf.aliases) {
		if(!strchr(modname, ':'))
			read_depends(dirname, modname, &list);

		/* We only use canned aliases as last resort. */
		if (list_empty(&list)
		    && !find_softdep(modname, conf.softdeps)
		    && !find_command(modname, conf.commands))
		{
			char *aliasfilename;


			nofail_asprintf(&aliasfilename, "%s/modules.alias",
					dirname);
			read_aliases(aliasfilename, modname, 0,
				     &conf.aliases);
			free(aliasfilename);
			/* builtin module? */
			if (!conf.aliases && module_builtin(dirname, modname) > 0) {
				failed |= handle_builtin_module(modname, error,
								flags);
				goto out;
			}
		}
	}

	filtered_aliases = apply_blacklist(conf.aliases, conf.blacklist);
	if(flags & mit_resolve_alias) {
		struct module_alias *aliases = filtered_aliases;

		for(; aliases; aliases=aliases->next)
			printf("%s\n", aliases->module);
		goto out;
	}
	if (filtered_aliases) {
		errfn_t err = error;
		struct module_alias *aliases = filtered_aliases;

		/* More than one alias?  Don't bail out on failure. */
		if (aliases->next)
			err = warn;
		while (aliases) {
			/* Add the options for this alias. */
			char *opts = NOFAIL(strdup(cmdline_opts));
			opts = add_extra_options(modname, opts, conf.options);

			read_depends(dirname, aliases->module, &list);
			failed |= handle_module(aliases->module,
				&list, newname, opts, conf.options,
				conf.softdeps, conf.commands, cmdline_opts,
				configname, dirname, err, flags);

			aliases = aliases->next;
			INIT_LIST_HEAD(&list);
		}
	} else {
		if (flags & mit_use_blacklist
		    && find_blacklist(modname, conf.blacklist))
			goto out;

		failed |= handle_module(modname, &list, newname, cmdline_opts,
			conf.options, conf.softdeps, conf.commands, cmdline_opts,
			configname, dirname, error, flags);
	}
out:
	free(modname);
	free_aliases(filtered_aliases);
	return failed;
}

static struct option options[] = { { "version", 0, NULL, 'V' },
				   { "verbose", 0, NULL, 'v' },
				   { "quiet", 0, NULL, 'q' },
				   { "syslog", 0, NULL, 's' },
				   { "show", 0, NULL, 'n' },
				   { "dry-run", 0, NULL, 'n' },
				   { "show-depends", 0, NULL, 'D' },
				   { "resolve-alias", 0, NULL, 'R' },
				   { "dirname", 1, NULL, 'd' },
				   { "set-version", 1, NULL, 'S' },
				   { "config", 1, NULL, 'C' },
				   { "name", 1, NULL, 'o' },
				   { "remove", 0, NULL, 'r' },
				   { "showconfig", 0, NULL, 'c' },
				   { "list", 0, NULL, 'l' },
				   { "type", 1, NULL, 't' },
				   { "all", 0, NULL, 'a' },
				   { "ignore-install", 0, NULL, 'i' },
				   { "ignore-remove", 0, NULL, 'i' },
				   { "use-blacklist", 0, NULL, 'b' },
				   { "force", 0, NULL, 'f' },
				   { "force-vermagic", 0, NULL, 1 },
				   { "force-modversion", 0, NULL, 2 },
				   { "first-time", 0, NULL, 3 },
				   { "dump-modversions", 0, NULL, 4 },
				   { NULL, 0, NULL, 0 } };

int main(int argc, char *argv[])
{
	struct utsname buf;
	struct stat statbuf;
	int opt;
	int dump_config = 0;
	int list_only = 0;
	int all = 0;
	int dump_modver = 0;
	unsigned int i, num_modules;
	char *type = NULL;
	const char *configname = NULL;
	char *basedir = "";
	char *cmdline_opts = NULL;
	char *newname = NULL;
	char *dirname;
	errfn_t error = fatal;
	int failed = 0;
	modprobe_flags_t flags = 0;

	recursion_depth = 0;

	/* Prepend options from environment. */
	argv = merge_args(getenv("MODPROBE_OPTIONS"), argv, &argc);

	uname(&buf);
	while ((opt = getopt_long(argc, argv, "Vvqsnd:C:o:rclt:aibf", options, NULL)) != -1){
		switch (opt) {
		case 'V':
			puts(PACKAGE " version " VERSION);
			exit(0);
		case 'v':
			add_to_env_var("-v");
			verbose = 1;
			break;
		case 'q':
			quiet = 1;
			add_to_env_var("-q");
			break;
		case 's':
			add_to_env_var("-s");
			logging = 1;
			break;
		case 'n':
			flags |= mit_dry_run;
			break;
		case 'd':
			basedir = optarg;
			break;
		case 'S':
			strncpy(buf.release, optarg, sizeof(buf.release));
			buf.release[sizeof(buf.release)-1] = '\0';
			break;
		case 'C':
			configname = optarg;
			add_to_env_var("-C");
			add_to_env_var(configname);
			break;
		case 'D':
			flags |= mit_dry_run;
			flags |= mit_ignore_loaded;
			verbose = 1;
			break;
		case 'R':
			flags |= mit_resolve_alias;
			break;
		case 'o':
			newname = optarg;
			break;
		case 'r':
			flags |= mit_remove;
			break;
		case 'c':
			dump_config = 1;
			break;
		case 'l':
			list_only = 1;
			break;
		case 't':
			type = optarg;
			break;
		case 'a':
			all = 1;
			error = warn;
			break;
		case 'i':
			flags |= mit_ignore_commands;
			break;
		case 'b':
			flags |= mit_use_blacklist;
			break;
		case 'f':
			flags |= mit_strip_vermagic;
			flags |= mit_strip_modversion;
			break;
		case 1:
			flags |= mit_strip_vermagic;
			break;
		case 2:
			flags |= mit_strip_modversion;
			break;
		case 3:
			flags |= mit_first_time;
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

	if (argc < optind + 1 && !dump_config && !list_only)
		print_usage(argv[0]);

	nofail_asprintf(&dirname, "%s%s/%s", basedir, MODULE_DIR, buf.release);

	/* Old-style -t xxx wildcard?  Only with -l. */
	if (list_only) {
		if (optind+1 < argc)
			fatal("Can't have multiple wildcards\n");
		/* fprintf(stderr, "man find\n"); return 1; */
		failed = do_wildcard(dirname, type, argv[optind]?:"*");
		goto out;
	}
	if (type)
		fatal("-t only supported with -l");

	if (dump_config) {
		char *aliasfilename, *symfilename;
		struct modprobe_conf conf = {};

		nofail_asprintf(&aliasfilename, "%s/modules.alias", dirname);
		nofail_asprintf(&symfilename, "%s/modules.symbols", dirname);

		parse_toplevel_config(configname, "", &conf, 1, 0);
		/* Read module options from kernel command line */
		parse_kcmdline(1, &conf.options);
		read_aliases(aliasfilename, "", 1, &conf.aliases);
		read_aliases(symfilename, "", 1, &conf.aliases);

		goto out;
	}

	if ((flags & mit_remove) || all) {
		num_modules = argc - optind;
		cmdline_opts = NOFAIL(strdup(""));
	} else {
		num_modules = 1;
		cmdline_opts = gather_options(argv+optind+1);
	}

	/* num_modules is always 1 except for -r or -a. */
	for (i = 0; i < num_modules; i++) {
		char *modname = argv[optind + i];

		if (dump_modver)
			dump_modversions(modname, error);
		else
			failed |= do_modprobe(modname, newname, cmdline_opts,
				configname, dirname, error, flags);

	}

out:
	if (logging)
		closelog();
	free(dirname);
	free(cmdline_opts);

	exit(failed);
}
