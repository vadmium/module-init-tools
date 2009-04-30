#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>

char *getline_wrapped(FILE *file, unsigned int *linenum);

void filename2modname(char *modname, const char *filename);
char *underscores(char *string);

const char *next_string(const char *string, unsigned long *secsize);

#define streq(a,b) (strcmp((a),(b)) == 0)
#define strstarts(a,start) (strncmp((a),(start), strlen(start)) == 0)
#define my_basename(path) ((strrchr((path), '/') ?: (path) - 1) + 1)

#endif
