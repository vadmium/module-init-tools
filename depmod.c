/* New simplified depmod without backwards compat stuff and not
   requiring ksyms.

   (C) 2002 Rusty Russell IBM Corporation
 */
#define _GNU_SOURCE /* asprintf */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <elf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/mman.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#include "util.h"
#include "zlibsupport.h"
#include "depmod.h"
#include "logging.h"
#include "index.h"
#include "moduleops.h"
#include "tables.h"
#include "config_filter.h"

#include "testing.h"

#ifndef MODULE_DIR
#define MODULE_DIR "/lib/modules/"
#endif

#ifndef MODULE_BUILTIN_KEY
#define MODULE_BUILTIN_KEY "built-in"
#endif

struct module_overrides
{
	/* Next override */
	struct module_overrides *next;

	/* overridden module */
	char *modfile;
};

struct module_search
{
	/* Next search */
	struct module_search *next;

	/* search path */
	char *search_path;
	size_t len;
};

static unsigned int skipchars;
static unsigned int make_map_files = 1; /* default to on */
static unsigned int force_map_files = 0; /* default to on */

#define SYMBOL_HASH_SIZE 1024
struct symbol
{
	struct symbol *next;
	struct module *owner;
	char name[0];
};

static struct symbol *symbolhash[SYMBOL_HASH_SIZE];

/* This is based on the hash agorithm from gdbm, via tdb */
static inline unsigned int tdb_hash(const char *name)
{
	unsigned value;	/* Used to compute the hash value.  */
	unsigned   i;	/* Used to cycle through random values. */

	/* Set the initial value from the key size. */
	for (value = 0x238F13AF * strlen(name), i=0; name[i]; i++)
		value = (value + (((unsigned char *)name)[i] << (i*5 % 24)));

	return (1103515243 * value + 12345);
}

void add_symbol(const char *name, struct module *owner)
{
	unsigned int hash;
	struct symbol *new = NOFAIL(malloc(sizeof *new + strlen(name) + 1));

	new->owner = owner;
	strcpy(new->name, name);

	hash = tdb_hash(name) % SYMBOL_HASH_SIZE;
	new->next = symbolhash[hash];
	symbolhash[hash] = new;
}

static int print_unknown;

struct module *find_symbol(const char *name, const char *modname, int weak)
{
	struct symbol *s;

	/* For our purposes, .foo matches foo.  PPC64 needs this. */
	if (name[0] == '.')
		name++;

	for (s = symbolhash[tdb_hash(name) % SYMBOL_HASH_SIZE]; s; s=s->next) {
		if (streq(s->name, name))
			return s->owner;
	}

	if (print_unknown && !weak)
		warn("%s needs unknown symbol %s\n", modname, name);

	return NULL;
}

void add_dep(struct module *mod, struct module *depends_on)
{
	unsigned int i;

	for (i = 0; i < mod->num_deps; i++)
		if (mod->deps[i] == depends_on)
			return;

	mod->deps = NOFAIL(realloc(mod->deps, sizeof(mod->deps[0])*(mod->num_deps+1)));
	mod->deps[mod->num_deps++] = depends_on;
}

static void load_system_map(const char *filename)
{
	FILE *system_map;
	char line[10240];
	const char ksymstr[] = "__ksymtab_";
	const int ksymstr_len = strlen(ksymstr);

	system_map = fopen(filename, "r");
	if (!system_map)
		fatal("Could not open '%s': %s\n", filename, strerror(errno));

	/* eg. c0294200 R __ksymtab_devfs_alloc_devnum */
	while (fgets(line, sizeof(line)-1, system_map)) {
		char *ptr;

		/* Snip \n */
		ptr = strchr(line, '\n');
		if (ptr)
			*ptr = '\0';

		ptr = strchr(line, ' ');
		if (!ptr || !(ptr = strchr(ptr + 1, ' ')))
			continue;

		/* Covers gpl-only and normal symbols. */
		if (strstarts(ptr+1, ksymstr))
			add_symbol(ptr+1+ksymstr_len, NULL);
	}

	fclose(system_map);

	/* __this_module is magic inserted by kernel loader. */
	add_symbol("__this_module", NULL);
	/* On S390, this is faked up too */
	add_symbol("_GLOBAL_OFFSET_TABLE_", NULL);
}

static struct option options[] = { { "all", 0, NULL, 'a' },
				   { "quick", 0, NULL, 'A' },
				   { "basedir", 1, NULL, 'b' },
				   { "errsyms", 0, NULL, 'e' },
				   { "filesyms", 1, NULL, 'F' },
				   { "help", 0, NULL, 'h' },
				   { "show", 0, NULL, 'n' },
				   { "dry-run", 0, NULL, 'n' },
				   { "quiet", 0, NULL, 'q' },
				   { "root", 0, NULL, 'r' },
				   { "unresolved-error", 0, NULL, 'u' },
				   { "verbose", 0, NULL, 'v' },
				   { "version", 0, NULL, 'V' },
				   { "config", 1, NULL, 'C' },
				   { "warn", 1, NULL, 'w' },
				   { "map", 0, NULL, 'm' },
				   { NULL, 0, NULL, 0 } };

/* Version number or module name?  Don't assume extension. */
static int is_version_number(const char *version)
{
	unsigned int dummy;

	return (sscanf(version, "%u.%u.%u", &dummy, &dummy, &dummy) == 3);
}

static int old_module_version(const char *version)
{
	/* Expect three part version. */
	unsigned int major, sub, minor;

	sscanf(version, "%u.%u.%u", &major, &sub, &minor);

	if (major > 2) return 0;
	if (major < 2) return 1;

	/* 2.x */
	if (sub > 5) return 0;
	if (sub < 5) return 1;

	/* 2.5.x */
	if (minor >= 48) return 0;
	return 1;
}

static void print_usage(const char *name)
{
	fprintf(stderr,
	"%s " VERSION " -- part of " PACKAGE "\n"
	"%s -[aA] [-n -e -v -q -V -r -u -w -m]\n"
	"      [-b basedirectory] [forced_version]\n"
	"depmod [-n -e -v -q -r -u -w] [-F kernelsyms] module1.ko module2.ko ...\n"
	"If no arguments (except options) are given, \"depmod -a\" is assumed\n"
	"\n"
	"depmod will output a dependancy list suitable for the modprobe utility.\n"
	"\n"
	"\n"
	"Options:\n"
	"\t-a, --all            Probe all modules\n"
	"\t-A, --quick          Only does the work if there's a new module\n"
	"\t-e, --errsyms        Report not supplied symbols\n"
	"\t-m, --map            Create the legacy map files\n"
	"\t-n, --show           Write the dependency file on stdout only\n"
	"\t-V, --version        Print the release version\n"
	"\t-v, --verbose        Enable verbose mode\n"
	"\t-w, --warn		Warn on duplicates\n"
	"\t-h, --help           Print this usage message\n"
	"\n"
	"The following options are useful for people managing distributions:\n"
	"\t-b basedirectory\n"
	"\t    --basedir basedirectory    Use an image of a module tree.\n"
	"\t-F kernelsyms\n"
	"\t    --filesyms kernelsyms      Use the file instead of the\n"
	"\t                               current kernel symbols.\n",
	"depmod", "depmod");
}

static int ends_in(const char *name, const char *ext)
{
	unsigned int namelen, extlen;

	/* Grab lengths */
	namelen = strlen(name);
	extlen = strlen(ext);

	if (namelen < extlen) return 0;

	if (streq(name + namelen - extlen, ext))
		return 1;
	return 0;
}

static struct module *grab_module(const char *dirname, const char *filename)
{
	struct module *new;

	new = NOFAIL(malloc(sizeof(*new)
			    + strlen(dirname?:"") + 1 + strlen(filename) + 1));
	if (dirname)
		sprintf(new->pathname, "%s/%s", dirname, filename);
	else
		strcpy(new->pathname, filename);
	new->basename = my_basename(new->pathname);

	INIT_LIST_HEAD(&new->dep_list);
	new->order = INDEX_PRIORITY_MIN;

	new->data = grab_file(new->pathname, &new->len);
	if (!new->data) {
		warn("Can't read module %s: %s\n",
		     new->pathname, strerror(errno));
		goto fail_data;
	}

	/* "\177ELF" <byte> where byte = 001 for 32-bit, 002 for 64 */
	if (memcmp(new->data, ELFMAG, SELFMAG) != 0) {
		warn("Module %s is not an elf object\n", new->pathname);
		goto fail;
	}

	switch (((char *)new->data)[EI_CLASS]) {
	case ELFCLASS32:
		new->ops = &mod_ops32;
		break;
	case ELFCLASS64:
		new->ops = &mod_ops64;
		break;
	default:
		warn("Module %s has elf unknown identifier %i\n",
		     new->pathname, ((char *)new->data)[EI_CLASS]);
		goto fail;
	}
	new->conv = ((char *)new->data)[EI_DATA] != native_endianness();
	return new;

fail:
	release_file(new->data, new->len);
fail_data:
	free(new);
	return NULL;
}

struct module_traverse
{
	struct module_traverse *prev;
	struct module *mod;
};

static int in_loop(struct module *mod, const struct module_traverse *traverse)
{
	const struct module_traverse *i;

	for (i = traverse; i; i = i->prev) {
		if (i->mod == mod)
			return 1;
	}
	return 0;
}

/* Assume we are doing all the modules, so only report each loop once. */
static void report_loop(const struct module *mod,
			const struct module_traverse *traverse)
{
	const struct module_traverse *i;

	/* Check that start is least alphabetically.  eg.  a depends
	   on b depends on a will get reported for a, not b.  */
	for (i = traverse->prev; i->prev; i = i->prev) {
		if (strcmp(mod->pathname, i->mod->pathname) > 0)
			return;
	}

	/* Is start in the loop?  If not, don't report now. eg. a
	   depends on b which depends on c which depends on b.  Don't
	   report when generating depends for a. */
	if (mod != i->mod)
		return;

	warn("Loop detected: %s ", mod->pathname);
	for (i = traverse->prev; i->prev; i = i->prev)
		fprintf(stderr, "needs %s ", i->mod->basename);
	fprintf(stderr, "which needs %s again!\n", i->mod->basename);
}

/* This is damn slow, but loops actually happen, and we don't want to
   just exit() and leave the user without any modules. */
static int has_dep_loop(struct module *module, struct module_traverse *prev)
{
	unsigned int i;
	struct module_traverse traverse = { .prev = prev, .mod = module };

	if (in_loop(module, prev)) {
		report_loop(module, &traverse);
		return 1;
	}

	for (i = 0; i < module->num_deps; i++)
		if (has_dep_loop(module->deps[i], &traverse))
			return 1;
	return 0;
}

/* Uniquifies and orders a dependency list. */
static void order_dep_list(struct module *start, struct module *mod)
{
	unsigned int i;

	for (i = 0; i < mod->num_deps; i++) {
		/* If it was previously depended on, move it to the
		   tail.  ie. if a needs b and c, and c needs b, we
		   must order b after c. */
		list_del(&mod->deps[i]->dep_list);
		list_add_tail(&mod->deps[i]->dep_list, &start->dep_list);
		order_dep_list(start, mod->deps[i]);
	}
}

static struct module *deleted = NULL;

static void del_module(struct module **modules, struct module *delme)
{
	struct module **i;

	/* Find pointer to it. */ 
	if (modules) {
		for (i = modules; *i != delme; i = &(*i)->next);
		
		*i = delme->next;
	}
	
	/* Save on a list to quiet valgrind.
	   Can't free - other modules may depend on them */
	delme->next = deleted;
	deleted = delme;
}

/* convert to relative path if possible */
static const char *compress_path(const char *path, const char *basedir)
{
	int len = strlen(basedir);

	if (strncmp(path, basedir, len) == 0)
		path += len + 1;
	return path;
}

static void output_deps(struct module *modules,
			FILE *out, char *dirname)
{
	struct module *i;

	for (i = modules; i; i = i->next) {
		struct list_head *j, *tmp;
		order_dep_list(i, i);

		fprintf(out, "%s:", compress_path(i->pathname, dirname));
		list_for_each_safe(j, tmp, &i->dep_list) {
			struct module *dep
				= list_entry(j, struct module, dep_list);
			fprintf(out, " %s",
			        compress_path(dep->pathname, dirname));
			list_del_init(j);
		}
		fprintf(out, "\n");
	}
}

/* warn whenever duplicate module aliases, deps, or symbols are found. */
int warn_dups = 0;

static void output_deps_bin(struct module *modules,
			FILE *out, char *dirname)
{
	struct module *i;
	struct index_node *index;
	char *line;
	char *p;

	index = index_create();

	for (i = modules; i; i = i->next) {
		struct list_head *j, *tmp;
		char modname[strlen(i->pathname)+1];
		
		order_dep_list(i, i);
		
		filename2modname(modname, i->pathname);
		nofail_asprintf(&line, "%s:",
		                compress_path(i->pathname, dirname));
		p = line;
		list_for_each_safe(j, tmp, &i->dep_list) {
			struct module *dep
				= list_entry(j, struct module, dep_list);
			nofail_asprintf(&line, "%s %s",
			                p,
			                compress_path(dep->pathname, dirname));
			free(p);
			p = line;
			list_del_init(j);
		}
		if (index_insert(index, modname, line, i->order) && warn_dups)
			warn("duplicate module deps:\n%s\n",line);
		free(line);
	}
	
	index_write(index, out);
	index_destroy(index);
}


static int smells_like_module(const char *name)
{
	return ends_in(name,".ko") || ends_in(name, ".ko.gz");
}

typedef struct module *(*do_module_t)(const char *dirname,
				      const char *filename,
				      struct module *next,
				      struct module_search *search,
				      struct module_overrides *overrides);

static int is_higher_priority(const char *newpath, const char *oldpath,
			      struct module_search *search,
			      struct module_overrides *overrides)
{
	struct module_search *tmp;
	struct module_overrides *ovtmp;
	int i = 0;
	int prio_builtin = -1;
	int prio_new = -1;
	int prio_old = -1;

/* The names already match, now we check for overrides and directory search
 * order
 */
	for (ovtmp = overrides; ovtmp != NULL; ovtmp = ovtmp->next) {
		if (streq(ovtmp->modfile, newpath))
			return 1;
		if (streq(ovtmp->modfile, oldpath))
			return 0;
	}
	for (i = 0, tmp = search; tmp != NULL; tmp = tmp->next, i++) {
		if (streq(tmp->search_path, MODULE_BUILTIN_KEY))
			prio_builtin = i;
		else if (strncmp(tmp->search_path, newpath, tmp->len) == 0)
			prio_new = i;
		else if (strncmp(tmp->search_path, oldpath, tmp->len) == 0)
			prio_old = i;
	}
	if (prio_new < 0)
		prio_new = prio_builtin;
	if (prio_old < 0)
		prio_old = prio_builtin;

	return prio_new > prio_old;
}


static struct module *do_module(const char *dirname,
				       const char *filename,
				       struct module *list,
				       struct module_search *search,
				       struct module_overrides *overrides)
{
	struct module *new, **i;

	new = grab_module(dirname, filename);
	if (!new)
		return list;

	/* Check if module is already in the list. */
	for (i = &list; *i; i = &(*i)->next) {

		if (streq((*i)->basename, filename)) {
			char newpath[strlen(dirname) + strlen("/")
				      + strlen(filename) + 1];

			sprintf(newpath, "%s/%s", dirname, filename);

			if (is_higher_priority(newpath, (*i)->pathname,search,
					       overrides)) {
				del_module(i, *i);
				
				new->next = *i;
				*i = new;
			} else
				del_module(NULL, new);

			return list;
		}
	}

	/* Not in the list already. Just prepend. */
	new->next = list;
	return new;
}

static struct module *grab_dir(const char *dirname,
			       DIR *dir,
			       struct module *next,
			       do_module_t do_mod,
			       struct module_search *search,
			       struct module_overrides *overrides)
{
	struct dirent *dirent;

	while ((dirent = readdir(dir)) != NULL) {
		if (smells_like_module(dirent->d_name))
			next = do_mod(dirname, dirent->d_name, next,
				      search, overrides);
		else if (!streq(dirent->d_name, ".")
			 && !streq(dirent->d_name, "..")
			 && !streq(dirent->d_name, "source")
			 && !streq(dirent->d_name, "build")) {

			DIR *sub;
			char subdir[strlen(dirname) + 1
				   + strlen(dirent->d_name) + 1];
			sprintf(subdir, "%s/%s", dirname, dirent->d_name);
			sub = opendir(subdir);
			if (sub) {
				next = grab_dir(subdir, sub, next, do_mod,
						search, overrides);
				closedir(sub);
			}
		}
	}
	return next;
}

static struct module *grab_basedir(const char *dirname,
				   struct module_search *search,
				   struct module_overrides *overrides)
{
	DIR *dir;
	struct module *list;

	dir = opendir(dirname);
	if (!dir) {
		warn("Couldn't open directory %s: %s\n",
		     dirname, strerror(errno));
		return NULL;
	}
	list = grab_dir(dirname, dir, NULL, do_module, search, overrides);
	closedir(dir);

	return list;
}

static struct module *sort_modules(const char *dirname, struct module *list)
{
	struct module *tlist = NULL, **tpos = &tlist;
	FILE *modorder;
	int dir_len = strlen(dirname) + 1;
	char file_name[dir_len + strlen("modules.order") + 1];
	char line[10240];
	unsigned int linenum = 0;

	sprintf(file_name, "%s/%s", dirname, "modules.order");

	modorder = fopen(file_name, "r");
	if (!modorder) {
		/* Older kernels don't generate modules.order.  Just
		   return if the file doesn't exist. */
		if (errno == ENOENT)
			return list;
		fatal("Could not open '%s': %s\n", file_name, strerror(errno));
	}

	sprintf(line, "%s/", dirname);

	/* move modules listed in modorder file to tlist in order */
	while (fgets(line, sizeof(line), modorder)) {
		struct module **pos, *mod;
		int len = strlen(line);

		linenum++;
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';

		for (pos = &list; (mod = *pos); pos = &(*pos)->next) {
			if (streq(line, mod->pathname + dir_len)) {
				mod->order = linenum;
				*pos = mod->next;
				mod->next = NULL;
				*tpos = mod;
				tpos = &mod->next;
				break;
			}
		}
	}

	/* append the rest */
	*tpos = list;

	fclose(modorder);

	return tlist;
}

static struct module *parse_modules(struct module *list)
{
	struct module *i;

	for (i = list; i; i = i->next) {
		i->ops->load_symbols(i);
		i->ops->fetch_tables(i);
	}
	
	for (i = list; i; i = i->next)
		i->ops->calculate_deps(i);
	
	/* Strip out modules with dependency loops. */
 again:
	for (i = list; i; i = i->next) {
		if (has_dep_loop(i, NULL)) {
			warn("Module %s ignored, due to loop\n",
			     i->pathname + skipchars);
			del_module(&list, i);
			goto again;
		}
	}
	
	return list;
}

/* Simply dump hash table. */
static void output_symbols(struct module *unused, FILE *out, char *dirname)
{
	unsigned int i;

	fprintf(out, "# Aliases for symbols, used by symbol_request().\n");
	for (i = 0; i < SYMBOL_HASH_SIZE; i++) {
		struct symbol *s;

		for (s = symbolhash[i]; s; s = s->next) {
			if (s->owner) {
				char modname[strlen(s->owner->pathname)+1];
				filename2modname(modname, s->owner->pathname);
				fprintf(out, "alias symbol:%s %s\n",
					s->name, modname);
			}
		}
	}
}

static void output_symbols_bin(struct module *unused, FILE *out, char *dirname)
{
	struct index_node *index;
	unsigned int i;
	char *alias;
	int duplicate;

	index = index_create();
	
	for (i = 0; i < SYMBOL_HASH_SIZE; i++) {
		struct symbol *s;

		for (s = symbolhash[i]; s; s = s->next) {
			if (s->owner) {
				char modname[strlen(s->owner->pathname)+1];
				filename2modname(modname, s->owner->pathname);
				nofail_asprintf(&alias, "symbol:%s", s->name);
				duplicate = index_insert(index, alias, modname,
							 s->owner->order);
				if (duplicate && warn_dups)
					warn("duplicate module syms:\n%s %s\n",
						alias, modname);
				free(alias);
			}
		}
	}
	
	index_write(index, out);
	index_destroy(index);
}

static void output_aliases(struct module *modules, FILE *out, char *dirname)
{
	struct module *i;
	const char *p;
	unsigned long size;

	fprintf(out, "# Aliases extracted from modules themselves.\n");
	for (i = modules; i; i = i->next) {
		char modname[strlen(i->pathname)+1];

		filename2modname(modname, i->pathname);

		/* Grab from old-style .modalias section. */
		for (p = i->ops->get_aliases(i, &size);
		     p;
		     p = next_string(p, &size))
			fprintf(out, "alias %s %s\n", p, modname);

		/* Grab form new-style .modinfo section. */
		for (p = i->ops->get_modinfo(i, &size);
		     p;
		     p = next_string(p, &size)) {
			if (strstarts(p, "alias="))
				fprintf(out, "alias %s %s\n",
					p + strlen("alias="), modname);
		}
	}
}

static void output_aliases_bin(struct module *modules, FILE *out, char *dirname)
{
	struct module *i;
	const char *p;
	char *alias;
	unsigned long size;
	struct index_node *index;
	int duplicate;

	index = index_create();
	
	for (i = modules; i; i = i->next) {
		char modname[strlen(i->pathname)+1];

		filename2modname(modname, i->pathname);

		/* Grab from old-style .modalias section. */
		for (p = i->ops->get_aliases(i, &size);
		     p;
		     p = next_string(p, &size)) {
			alias = NOFAIL(strdup(p));
			underscores(alias);
			duplicate = index_insert(index, alias, modname, i->order);
			if (duplicate && warn_dups)
				warn("duplicate module alias:\n%s %s\n",
					alias, modname);
			free(alias);
		}

		/* Grab from new-style .modinfo section. */
		for (p = i->ops->get_modinfo(i, &size);
		     p;
		     p = next_string(p, &size)) {
			if (strstarts(p, "alias=")) {
				alias = NOFAIL(strdup(p + strlen("alias=")));
				underscores(alias);
				duplicate = index_insert(index, alias, modname, i->order);
				if (duplicate && warn_dups)
					warn("duplicate module alias:\n%s %s\n",
						alias, modname);
				free(alias);
			}
		}
	}
	
	index_write(index, out);
	index_destroy(index);
}

struct depfile {
	char *name;
	void (*func)(struct module *, FILE *, char *dirname);
	int map_file;
};

static struct depfile depfiles[] = {
	{ "modules.dep", output_deps, 0 }, /* This is what we check for '-A'. */
	{ "modules.dep.bin", output_deps_bin, 0 },
	{ "modules.pcimap", output_pci_table, 1 },
	{ "modules.usbmap", output_usb_table, 1 },
	{ "modules.ccwmap", output_ccw_table, 1 },
	{ "modules.ieee1394map", output_ieee1394_table, 1 },
	{ "modules.isapnpmap", output_isapnp_table, 1 },
	{ "modules.inputmap", output_input_table, 1 },
	{ "modules.ofmap", output_of_table, 1 },
	{ "modules.seriomap", output_serio_table, 1 },
	{ "modules.alias", output_aliases, 0 },
	{ "modules.alias.bin", output_aliases_bin, 0 },
	{ "modules.symbols", output_symbols, 0 },
	{ "modules.symbols.bin", output_symbols_bin, 0 }
};

/* If we can't figure it out, it's safe to say "true". */
static int any_modules_newer(const char *dirname, time_t mtime)
{
	DIR *dir;
	struct dirent *dirent;

	dir = opendir(dirname);
	if (!dir)
		return 1;

	while ((dirent = readdir(dir)) != NULL) {
		struct stat st;
		char file[strlen(dirname) + 1 + strlen(dirent->d_name) + 1];

		if (streq(dirent->d_name, ".") || streq(dirent->d_name, ".."))
			continue;

		sprintf(file, "%s/%s", dirname, dirent->d_name);
		if (lstat(file, &st) != 0)
			goto ret_true;

		if (smells_like_module(dirent->d_name)) {
			if (st.st_mtime > mtime)
				goto ret_true;
		} else if (S_ISDIR(st.st_mode)) {
			if (any_modules_newer(file, mtime))
				goto ret_true;
		}
	}
	closedir(dir);
	return 0;

ret_true:
	closedir(dir);
	return 1;
}

static int depfile_out_of_date(const char *dirname)
{
	struct stat st;
	char depfile[strlen(dirname) + 1 + strlen(depfiles[0].name) + 1];

	sprintf(depfile, "%s/%s", dirname, depfiles[0].name);

	if (stat(depfile, &st) != 0)
		return 1;

	return any_modules_newer(dirname, st.st_mtime);
}

static char *strsep_skipspace(char **string, char *delim)
{
	if (!*string)
		return NULL;
	*string += strspn(*string, delim);
	return strsep(string, delim);
}

static struct module_search *add_search(const char *search_path,
					size_t len,
					struct module_search *search)
{

	struct module_search *new;
	
	new = NOFAIL(malloc(sizeof(*new)));
	new->search_path = NOFAIL(strdup(search_path));
	new->len = len;
	new->next = search;

	return new;
	
}

static struct module_overrides *add_override(const char *modfile,
					     struct module_overrides *overrides)
{

	struct module_overrides *new;
	
	new = NOFAIL(malloc(sizeof(*new)));
	new->modfile = NOFAIL(strdup(modfile));
	new->next = overrides;

	return new;
	
}

static int parse_config_scan(const char *filename,
			     const char *basedir,
			     const char *kernelversion,
			     struct module_search **search,
			     struct module_overrides **overrides);

static int parse_config_file(const char *filename,
			     const char *basedir,
			     const char *kernelversion,
			     struct module_search **search,
			     struct module_overrides **overrides)
{
	char *line;
	unsigned int linenum = 0;
	FILE *cfile;

	cfile = fopen(filename, "r");
	if (!cfile) {
		if (errno != ENOENT)
			fatal("could not open '%s', reason: %s\n", filename,
			      strerror(errno));
		return 0;
	}

	while ((line = getline_wrapped(cfile, &linenum)) != NULL) {
		char *ptr = line;
		char *cmd, *modname;

		cmd = strsep_skipspace(&ptr, "\t ");

		if (cmd == NULL || cmd[0] == '#' || cmd[0] == '\0') {
			free(line);
			continue;
		}

		if (streq(cmd, "search")) {
			char *search_path;
			
			while ((search_path = strsep_skipspace(&ptr, "\t "))) {
				char *dirname;
				size_t len;

				if (strcmp(search_path,
						MODULE_BUILTIN_KEY) == 0) {
					*search = add_search(MODULE_BUILTIN_KEY,
							     0, *search);
					continue;
				}
				nofail_asprintf(&dirname, "%s%s%s/%s", basedir,
					MODULE_DIR, kernelversion, search_path);
				len = strlen(dirname);
				*search = add_search(dirname, len, *search);
				free(dirname);
			}
		} else if (streq(cmd, "override")) {
			char *pathname = NULL, *version, *subdir;
			modname = strsep_skipspace(&ptr, "\t ");
			version = strsep_skipspace(&ptr, "\t ");
			subdir = strsep_skipspace(&ptr, "\t ");

			if (strcmp(version, kernelversion) != 0 &&
			    strcmp(version, "*") != 0)
				continue;

			nofail_asprintf(&pathname, "%s%s%s/%s/%s.ko", basedir,
				MODULE_DIR, kernelversion, subdir, modname);

			*overrides = add_override(pathname, *overrides);
			free(pathname);
		} else if (streq(cmd, "include")) {
			char *newfilename;

			newfilename = strsep_skipspace(&ptr, "\t ");
			if (!newfilename) {
				grammar(cmd, filename, linenum);
			} else {
				warn("\"include %s\" is deprecated, "
				     "please use /etc/depmod.d\n", newfilename);
				if (strstarts(newfilename, "/etc/depmod.d")) {
					warn("\"include /etc/depmod.d\" is "
					     "the default, ignored\n");
				} else {
					if (!parse_config_scan(newfilename, basedir,
							       kernelversion,
							       search, overrides))
					warn("Failed to open included"
					     " config file %s: %s\n",
					     newfilename, strerror(errno));
				}
			}
		} else if (streq(cmd, "make_map_files")) {
			char *option;

			option = strsep_skipspace(&ptr, "\t ");
			if (!option)
				grammar(cmd, filename, linenum);
			else {
				if (streq(option, "yes"))
					make_map_files = 1;
				else if (streq(option, "no"))
					make_map_files = 0;
				else
					grammar(cmd, filename, linenum);
			}
                } else
                        grammar(cmd, filename, linenum);

                free(line);
        }
        fclose(cfile);
        return 1;
}

static int parse_config_scan(const char *filename,
			     const char *basedir,
			     const char *kernelversion,
			     struct module_search **search,
			     struct module_overrides **overrides)
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
			if (len < 6 || strcmp(&i->d_name[len-5], ".conf") != 0)
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
			if (!parse_config_file(cfgfile, basedir, kernelversion,
					       search, overrides))
				warn("Failed to open config file "
				     "%s: %s\n", fe->name, strerror(errno));
			free(cfgfile);
			list_del(&fe->node);
			free(fe);
		}

		ret = 1;
	} else {
		if (parse_config_file(filename, basedir, kernelversion, search,
				      overrides))
			ret = 1;
	}

	return ret;
}

static void parse_toplevel_config(const char *filename,
				  const char *basedir,
				  const char *kernelversion,
				  struct module_search **search,
				  struct module_overrides **overrides)
{
	if (filename) {
		if (!parse_config_scan(filename, basedir, kernelversion, search,
				 overrides))
			fatal("Failed to open config file %s: %s\n",
			      filename, strerror(errno));
		return;
	}

	/* deprecated config file */
	if (parse_config_file("/etc/depmod.conf", basedir, kernelversion,
			      search, overrides) > 0)
		warn("Deprecated config file /etc/depmod.conf, "
		      "all config files belong into /etc/depmod.d/.\n");

	/* default config */
	parse_config_scan("/etc/depmod.d", basedir, kernelversion,
			  search, overrides);
}

/* Local to main, but not freed on exit.  Keep valgrind quiet. */
struct module *list = NULL;
struct module_search *search = NULL;
struct module_overrides *overrides = NULL;

int main(int argc, char *argv[])
{
	int opt, all = 0, maybe_all = 0, doing_stdout = 0;
	char *basedir = "", *dirname, *version, *badopt = NULL,
		*system_map = NULL;
	int i;
	const char *config = NULL;

	if (native_endianness() == 0)
		abort();

	/* Don't print out any errors just yet, we might want to exec
           backwards compat version. */
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "ab:ArehnqruvVF:C:wm", options, NULL))
	       != -1) {
		switch (opt) {
		case 'a':
			all = 1;
			break;
		case 'b':
			basedir = optarg;
			skipchars = strlen(basedir);
			break;
		case 'A':
			maybe_all = 1;
			break;
		case 'F':
			system_map = optarg;
			break;
		case 'e':
			print_unknown = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'u':
		case 'q':
		case 'r':
			break;
		case 'C':
			config = optarg;
			break;
		case 'h':
			print_usage(argv[0]);
			exit(0);
			break;
		case 'n':
			doing_stdout = 1;
			break;
		case 'V':
			printf("%s %s\n", PACKAGE, VERSION);
			exit(0);
		case 'w':
			warn_dups = 1;
			break;
		case 'm':
			force_map_files = 1;
			break;
		default:
			badopt = argv[optind-1];
		}
	}

	/* We can't print unknowns without a System.map */
	if (!system_map)
		print_unknown = 0;
	else
		load_system_map(system_map);

	/* They can specify the version naked on the command line */
	if (optind < argc && is_version_number(argv[optind])) {
		version = NOFAIL(strdup(argv[optind]));
		optind++;
	} else {
		struct utsname buf;
		uname(&buf);
		version = NOFAIL(strdup(buf.release));
	}

	/* Check for old version. */
	if (old_module_version(version)) {
		fprintf(stderr, "Kernel version %s requires old depmod\n",
			version);
		exit(2);
	}

	if (badopt) {
		fprintf(stderr, "%s: malformed/unrecognized option '%s'\n",
			argv[0], badopt);
		print_usage(argv[0]);
		exit(1);
	}

	/* Depmod -a by default if no names. */
	if (optind == argc)
		all = 1;

	nofail_asprintf(&dirname, "%s%s%s", basedir, MODULE_DIR, version);

	if (maybe_all) {
		if (!doing_stdout && !depfile_out_of_date(dirname))
			exit(0);
		all = 1;
	}

	parse_toplevel_config(config, basedir, version, &search, &overrides);

	/* For backward compatibility add "updates" to the head of the search
	 * list here. But only if there was no "search" option specified.
	 */
	if (!search) {
		char *dirname;
		size_t len;

		nofail_asprintf(&dirname, "%s%s%s/updates", basedir,
				MODULE_DIR, version);
		len = strlen(dirname);
		search = add_search(dirname, len, search);
	}
	if (!all) {
		/* Do command line args. */
		for (opt = optind; opt < argc; opt++) {
			struct module *new;

			if (argv[opt][0] != '/')
				fatal("modules must be specified using absolute paths.\n"
					"\"%s\" is a relative path\n", argv[opt]);

			new = grab_module(NULL, argv[opt]);
			if (!new) {
				/* cmd-line specified modules must exist */
				fatal("grab_module() failed for module %s\n", argv[opt]);
			}
			new->next = list;
			list = new;
		}
	} else {
		list = grab_basedir(dirname,search,overrides);
	}
	list = sort_modules(dirname,list);
	list = parse_modules(list);

	for (i = 0; i < sizeof(depfiles)/sizeof(depfiles[0]); i++) {
		FILE *out;
		struct depfile *d = &depfiles[i];
		char depname[strlen(dirname) + 1 + strlen(d->name) + 1];
		char tmpname[strlen(dirname) + 1 + strlen(d->name) +
						strlen(".temp") + 1];

		if (d->map_file && !make_map_files && !force_map_files)
			continue;

		sprintf(depname, "%s/%s", dirname, d->name);
		sprintf(tmpname, "%s/%s.temp", dirname, d->name);
		if (!doing_stdout) {
			out = fopen(tmpname, "w");
			if (!out)
				fatal("Could not open %s for writing: %s\n",
					tmpname, strerror(errno));
		} else {
			out = stdout;
			if (ends_in(depname, ".bin"))
				continue;
		}
		d->func(list, out, dirname);
		if (!doing_stdout) {
			fclose(out);
			if (rename(tmpname, depname) < 0)
				fatal("Could not rename %s into %s: %s\n",
					tmpname, depname, strerror(errno));
		}
	}

	free(dirname);
	free(version);
	
	return 0;
}
