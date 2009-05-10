/* Extract module info: useful for both the curious and for scripts. */
#define _GNU_SOURCE /* asprintf rocks */
#include <elf.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/mman.h>

#include "util.h"
#include "elfops.h"
#include "zlibsupport.h"
#include "testing.h"

#ifndef MODULE_DIR
#define MODULE_DIR "/lib/modules"
#endif

struct param
{
	struct param *next;
	const char *name; 	/* Terminated by a colon */
	const char *param;
	const char *type;
};

static struct param *add_param(const char *name, struct param **list)
{
	struct param *i;
	unsigned int namelen = strcspn(name, ":") + 1;

	for (i = *list; i; i = i->next)
		if (strncmp(i->name, name, namelen) == 0)
			return i;
	i = malloc(sizeof(*i) + namelen+1);
	strncpy((char *)(i + 1), name, namelen);
	((char *)(i + 1))[namelen] = '\0';
	i->name = (char *)(i + 1);
	i->param = NULL;
	i->type = NULL;
	i->next = *list;
	*list = i;
	return i;
}

static void print_tag(const char *tag, const char *info, unsigned long size,
		      const char *filename, char sep)
{
	unsigned int taglen = strlen(tag);

	if (streq(tag, "filename")) {
		printf("%s%c", filename, sep);
		return;
	}

	for (; info; info = next_string(info, &size))
		if (strncmp(info, tag, taglen) == 0 && info[taglen] == '=')
			printf("%s%c", info + taglen + 1, sep);
}

static void print_all(const char *info, unsigned long size,
		      const char *filename, char sep)
{
	struct param *i, *params = NULL;

	printf("%-16s%s%c", "filename:", filename, sep);
	for (; info; info = next_string(info, &size)) {
		char *eq, *colon;

		/* We expect this in parm and parmtype. */
		colon = strchr(info, ':');

		/* We store these for handling at the end */
		if (strstarts(info, "parm=") && colon) {
			i = add_param(info + strlen("parm="), &params);
			i->param = colon + 1;
			continue;
		}
		if (strstarts(info, "parmtype=") && colon) {
			i = add_param(info + strlen("parmtype="), &params);
			i->type = colon + 1;
			continue;
		}

		if (!sep) {
			printf("%s%c", info, sep);
			continue;
		}

		eq = strchr(info, '=');
		/* Warn if no '=' maybe? */
		if (eq) {
			char tag[eq - info + 2];
			strncpy(tag, info, eq - info);
			tag[eq-info] = ':';
			tag[eq-info+1] = '\0';
			printf("%-16s%s%c", tag, eq+1, sep);
		}
	}

	/* Now show parameters. */
	for (i = params; i; i = i->next) {
		if (!i->param)
			printf("%-16s%s%s%c", "parm:", i->name, i->type, sep);
		else if (i->type)
			printf("%-16s%s%s (%s)%c",
			       "parm:", i->name, i->param, i->type, sep);
		else 
			printf("%-16s%s%s%c", "parm:", i->name, i->param, sep);
	}
}

static struct option options[] =
{
	{"author", 0, 0, 'a'},
	{"description", 0, 0, 'd'},
	{"license", 0, 0, 'l'},
	{"parameters", 0, 0, 'p'},
	{"filename", 0, 0, 'n'},
	{"version", 0, 0, 'V'},
	{"help", 0, 0, 'h'},
	{"null", 0, 0, '0'},
	{"field", 1, 0, 'F'},
	{0, 0, 0, 0}
};

/* - and _ are equivalent, and expect suffix. */
static int name_matches(const char *line, const char *end, const char *modname)
{
	unsigned int i;
	char *p;

	/* Ignore comment lines */
	if (line[strspn(line, "\t ")] == '#')
		return 0;

	/* Find last / before colon. */
	p = memchr(line, ':', end - line);
	if (!p)
		return 0;
	while (p > line) {
		if (*p == '/') {
			p++;
			break;
		}
		p--;
	}

	for (i = 0; modname[i]; i++) {
		/* Module names can't have colons. */
		if (modname[i] == ':')
			continue;
		if (modname[i] == p[i])
			continue;
		if (modname[i] == '_' && p[i] == '-')
			continue;
		if (modname[i] == '-' && p[i] == '_')
			continue;
		return 0;
	}
	/* Must match all the way to the extension */
	return (p[i] == '.');
}

static char *next_line(char *p, const char *end)
{
	char *eol;

	eol = memchr(p, '\n', end - p);
	if (eol)
		return eol + 1;
	return (char *)end + 1;
}

static void *grab_module(const char *name, unsigned long *size, char**filename,
	const char *kernel, const char *basedir)
{
	char *data;
	struct utsname buf;
	char *depname, *p, *moddir;

	if (strchr(name, '.') || strchr(name, '/')) {
		data = grab_file(name, size);
		if (data) {
			*filename = strdup(name);
			return data;
		} else {
			fprintf(stderr, "modinfo: could not open %s: %s\n",
				name, strerror(errno));
			return NULL;
		}
	}

	if (kernel) {
		if (strlen(basedir))
			asprintf(&moddir, "%s/%s/%s",
				basedir, MODULE_DIR, kernel);
		else
			asprintf(&moddir, "%s/%s",
				MODULE_DIR, kernel);
	} else {
		uname(&buf);
		if (strlen(basedir))
			asprintf(&moddir, "%s/%s/%s",
			 	basedir, MODULE_DIR, buf.release);
		else
			asprintf(&moddir, "%s/%s",
				MODULE_DIR, buf.release);
	}
	asprintf(&depname, "%s/%s", moddir, "modules.dep");

	/* Search for it in modules.dep. */
	data = grab_file(depname, size);
	if (!data) {
		fprintf(stderr, "modinfo: could not open %s\n", depname);
		free(depname);
		return NULL;
	}
	free(depname);

	for (p = data; p < data + *size; p = next_line(p, data + *size)) {
		if (name_matches(p, data + *size, name)) {
			int namelen = strcspn(p, ":");

			if ('/' == p[0]) { /* old style deps - absolute path */
				*filename = malloc(namelen + strlen(basedir)+2);
				if (strlen(basedir)) {
					sprintf(*filename, "%s/", basedir);
					memcpy(*filename+strlen(basedir)+1,p,
						namelen);
					(*filename)[namelen
						+strlen(basedir)+1] = '\0';
				} else {
					memcpy(*filename,p,namelen);
					(*filename)[namelen] = '\0';
				}
			} else {
				*filename = malloc(namelen + strlen(moddir)+2);
				sprintf(*filename, "%s/", moddir);
				memcpy(*filename+strlen(moddir)+1, p,namelen);
				(*filename)[namelen+strlen(moddir)+1] ='\0';
			}
			release_file(data, *size);
			data = grab_file(*filename, size);
			if (!data)
				fprintf(stderr,
					"modinfo: could not open %s: %s\n",
					*filename, strerror(errno));
			return data;
		}
	}
	release_file(data, *size);
	fprintf(stderr, "modinfo: could not find module %s\n", name);
	return NULL;
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [-0][-F field][-k kernelversion][-b basedir]  module...\n"
		" Prints out the information about one or more module(s).\n"
		" If a fieldname is given, just print out that field (or nothing if not found).\n"
		" Otherwise, print all information out in a readable form\n"
		" If -0 is given, separate with nul, not newline.\n"
		" If -b is given, use an image of the module tree.\n",
		name);
}

int main(int argc, char *argv[])
{
	const char *field = NULL;
	const char *kernel = NULL;
	char sep = '\n';
	unsigned long infosize = 0;
	int opt, ret = 0;
	char *basedir = "";

	if (native_endianness() == 0)
		abort();

	while ((opt = getopt_long(argc,argv,"adlpVhn0F:k:b:",options,NULL)) >= 0){
		switch (opt) {
		case 'a': field = "author"; break;
		case 'd': field = "description"; break;
		case 'l': field = "license"; break;
		case 'p': field = "parm"; break;
		case 'n': field = "filename"; break;
		case 'V': printf(PACKAGE " version " VERSION "\n"); exit(0);
		case 'F': field = optarg; break;
		case '0': sep = '\0'; break;
		case 'k': kernel = optarg; break;
		case 'b': basedir = optarg; break;
		case 'h': usage(argv[0]); exit(0); break;
		default:
			usage(argv[0]); exit(1);
		}
	}
	if (argc < optind + 1) {
		usage(argv[0]);
		exit(1);
	}

	for (opt = optind; opt < argc; opt++) {
		void *info, *mod;
		unsigned long modulesize;
		char *filename;

		mod = grab_module(argv[opt], &modulesize, &filename,
				  kernel, basedir);
		if (!mod) {
			ret = 1;
			continue;
		}

		info = get_section(mod, modulesize, ".modinfo", &infosize);
		if (!info) {
			release_file(mod, modulesize);
			free(filename);
			continue;
		}
		if (field)
			print_tag(field, info, infosize, filename, sep);
		else
			print_all(info, infosize, filename, sep);
		release_file(mod, modulesize);
		free(filename);
	}
	return ret;
}
