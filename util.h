#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>

char *getline_wrapped(FILE *file, unsigned int *linenum);

void filename2modname(char *modname, const char *filename);
char *underscores(char *string);

const char *next_string(const char *string, unsigned long *secsize);

/*
 * Change endianness of x if conv is true.
 */
#define END(x, conv)							\
({									\
	typeof(x) __x;							\
	if (conv) __swap_bytes(&(x), &(__x), sizeof(__x));		\
	else __x = (x);							\
	__x;								\
})

static inline void __swap_bytes(const void *src, void *dest, unsigned int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
		((unsigned char*)dest)[i] = ((unsigned char*)src)[size - i-1];
}

int native_endianness(void);

#define streq(a,b) (strcmp((a),(b)) == 0)
#define strstarts(a,start) (strncmp((a),(start), strlen(start)) == 0)
#define my_basename(path) ((strrchr((path), '/') ?: (path) - 1) + 1)

#endif
