#ifndef MODINITTOOLS_DEPMOD_H
#define MODINITTOOLS_DEPMOD_H
#include "list.h"

/* Tables extracted from module by ops->fetch_tables(). */
struct module_tables {
	unsigned int pci_size;
	void *pci_table;
	unsigned int usb_size;
	void *usb_table;
	unsigned int ieee1394_size;
	void *ieee1394_table;
	unsigned int ccw_size;
	void *ccw_table;
	unsigned int pnp_size;
	void *pnp_table;
	unsigned int pnp_card_size;
	unsigned int pnp_card_offset;
	void *pnp_card_table;
	unsigned int input_size;
	void *input_table;
	unsigned int input_table_size;
	unsigned int serio_size;
	void *serio_table;
	unsigned int of_size;
	void *of_table;
};

struct elf_file
{
	/* File operations */
	struct module_ops *ops;

	/* Convert endian? */
	int conv;

	/* File contents and length. */
	void *data;
	unsigned long len;
};


struct module;

struct module
{
	/* Next module in list of all modules */
	struct module *next;

	/* Dependencies: filled in by ops->calculate_deps() */
	unsigned int num_deps;
	struct module **deps;

	/* Set while we are traversing dependencies */
	struct list_head dep_list;

	/* Line number in modules.order (or INDEX_PRIORITY_MIN) */
	unsigned int order;

	/* Tables extracted from module by ops->fetch_tables(). */
	struct module_tables tables;

	struct elf_file file;

	char *basename; /* points into pathname */
	char pathname[0];
};

#endif /* MODINITTOOLS_DEPMOD_H */
