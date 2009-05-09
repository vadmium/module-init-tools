/* Load the given section: NULL on error. */
static void *PERBIT(load_section)(ElfPERBIT(Ehdr) *hdr,
			    const char *secname,
			    unsigned long *secsize,
			    int conv)
{
	return PERBIT(get_section)(hdr, 0, secname, secsize, conv);
}

static struct string_table *PERBIT(load_symbols)(struct module *module)
{
	struct PERBIT(kernel_symbol) *ksyms;
	struct string_table *symtbl;
	char *ksymstrings;
	unsigned long i, size;

	symtbl = NULL;

	/* New-style: strings are in this section. */
	ksymstrings = PERBIT(load_section)(module->data, "__ksymtab_strings",
					   &size, module->conv);
	if (ksymstrings) {
		unsigned int i = 0;
		for (;;) {
			/* Skip any zero padding. */
			while (!ksymstrings[i])
				if (++i >= size)
					return symtbl;
			symtbl = NOFAIL(strtbl_add(ksymstrings + i, symtbl));
			i += strlen(ksymstrings+i);
		}
		/* GPL symbols too */
		ksymstrings = PERBIT(load_section)(module->data,
						   "__ksymtab_strings_gpl",
						   &size, module->conv);
		for (;;) {
			/* Skip any zero padding. */
			while (!ksymstrings[i])
				if (++i >= size)
					return symtbl;
			symtbl = NOFAIL(strtbl_add(ksymstrings + i, symtbl));
			i += strlen(ksymstrings+i);
		}
		return symtbl;
	}

	/* Old-style. */
	ksyms = PERBIT(load_section)(module->data, "__ksymtab", &size,
				     module->conv);
	for (i = 0; i < size / sizeof(struct PERBIT(kernel_symbol)); i++)
		symtbl = NOFAIL(strtbl_add(ksyms[i].name, symtbl));
	ksyms = PERBIT(load_section)(module->data, "__gpl_ksymtab", &size,
				     module->conv);
	for (i = 0; i < size / sizeof(struct PERBIT(kernel_symbol)); i++)
		symtbl = NOFAIL(strtbl_add(ksyms[i].name, symtbl));

	return symtbl;
}

static char *PERBIT(get_aliases)(struct module *module, unsigned long *size)
{
	return PERBIT(load_section)(module->data, ".modalias", size,
				    module->conv);
}

static char *PERBIT(get_modinfo)(struct module *module, unsigned long *size)
{
	return PERBIT(load_section)(module->data, ".modinfo", size,
				    module->conv);
}

#ifndef STT_REGISTER
#define STT_REGISTER    13              /* Global register reserved to app. */
#endif

static struct string_table *PERBIT(load_dep_syms)(struct module *module,
						  struct string_table **types)
{
	unsigned int i;
	unsigned long size;
	char *strings;
	ElfPERBIT(Sym) *syms;
	ElfPERBIT(Ehdr) *hdr;
	int handle_register_symbols;
	struct string_table *names;

	names = NULL;
	*types = NULL;

	strings = PERBIT(load_section)(module->data, ".strtab", &size,
				       module->conv);
	syms = PERBIT(load_section)(module->data, ".symtab", &size,
				    module->conv);

	if (!strings || !syms) {
		warn("Couldn't find symtab and strtab in module %s\n",
		     module->pathname);
		return NULL;
	}

	hdr = module->data;
	handle_register_symbols = 0;
	if (END(hdr->e_machine, module->conv) == EM_SPARC ||
	    END(hdr->e_machine, module->conv) == EM_SPARCV9)
		handle_register_symbols = 1;

	for (i = 1; i < size / sizeof(syms[0]); i++) {
		if (END(syms[i].st_shndx, module->conv) == SHN_UNDEF) {
			/* Look for symbol */
			const char *name;
			int weak;

			name = strings + END(syms[i].st_name, module->conv);

			/* Not really undefined: sparc gcc 3.3 creates
                           U references when you have global asm
                           variables, to avoid anyone else misusing
                           them. */
			if (handle_register_symbols
			    && (ELFPERBIT(ST_TYPE)(END(syms[i].st_info,
						       module->conv))
				== STT_REGISTER))
				continue;

			weak = (ELFPERBIT(ST_BIND)(END(syms[i].st_info,
						       module->conv))
				== STB_WEAK);
			names = strtbl_add(name, names);
			*types = strtbl_add(weak ? "W" : "U", *types);
		}
	}
	return names;
}

static void *PERBIT(deref_sym)(ElfPERBIT(Ehdr) *hdr,
			       ElfPERBIT(Shdr) *sechdrs,
			       ElfPERBIT(Sym) *sym,
			       unsigned int *secsize,
			       int conv)
{
	/* In BSS?  Happens for empty device tables on
	 * recent GCC versions. */
	if (END(sechdrs[END(sym->st_shndx, conv)].sh_type,conv) == SHT_NOBITS)
		return NULL;
	
	if (secsize)
		*secsize = END(sym->st_size, conv);
	return (void *)hdr
		+ END(sechdrs[END(sym->st_shndx, conv)].sh_offset, conv)
		+ END(sym->st_value, conv);
}

/* FIXME: Check size, unless we end up using aliases anyway --RR */
static void PERBIT(fetch_tables)(struct module *module)
{
	unsigned int i;
	unsigned long size;
	char *strings;
	ElfPERBIT(Ehdr) *hdr;
	ElfPERBIT(Sym) *syms;
	ElfPERBIT(Shdr) *sechdrs;
	
	hdr = module->data;

	sechdrs = (void *)hdr + END(hdr->e_shoff, module->conv);
	strings = PERBIT(load_section)(hdr, ".strtab", &size, module->conv);
	syms = PERBIT(load_section)(hdr, ".symtab", &size, module->conv);

	/* Don't warn again: we already have above */
	if (!strings || !syms)
		return;
		
	module->pci_table = NULL;
	module->usb_table = NULL;
	module->ccw_table = NULL;
	module->ieee1394_table = NULL;
	module->pnp_table = NULL;
	module->pnp_card_table = NULL;
	module->input_table = NULL;
	module->serio_table = NULL;
	module->of_table = NULL;

	for (i = 0; i < size / sizeof(syms[0]); i++) {
		char *name = strings + END(syms[i].st_name, module->conv);
		
		if (!module->pci_table && streq(name, "__mod_pci_device_table")) {
			module->pci_size = PERBIT(PCI_DEVICE_SIZE);
			module->pci_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, module->conv);
		}
		else if (!module->usb_table && streq(name, "__mod_usb_device_table")) {
			module->usb_size = PERBIT(USB_DEVICE_SIZE);
			module->usb_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, module->conv);
		}
		else if (!module->ccw_table && streq(name, "__mod_ccw_device_table")) {
			module->ccw_size = PERBIT(CCW_DEVICE_SIZE);
			module->ccw_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, module->conv);
		}
		else if (!module->ieee1394_table && streq(name, "__mod_ieee1394_device_table")) {
			module->ieee1394_size = PERBIT(IEEE1394_DEVICE_SIZE);
			module->ieee1394_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
								   NULL, module->conv);
		}
		else if (!module->pnp_table && streq(name, "__mod_pnp_device_table")) {
			module->pnp_size = PERBIT(PNP_DEVICE_SIZE);
			module->pnp_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, module->conv);
		}
		else if (!module->pnp_card_table && streq(name, "__mod_pnp_card_device_table")) {
			module->pnp_card_size = PERBIT(PNP_CARD_DEVICE_SIZE);
			module->pnp_card_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
								   NULL, module->conv);
			module->pnp_card_offset = PERBIT(PNP_CARD_DEVICE_OFFSET);
		}
		else if (!module->input_table && streq(name, "__mod_input_device_table")) {
			module->input_size = PERBIT(INPUT_DEVICE_SIZE);
			module->input_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							        &module->input_table_size,
							        module->conv);
		}
		else if (!module->serio_table && streq(name, "__mod_serio_device_table")) {
			module->serio_size = PERBIT(SERIO_DEVICE_SIZE);
			module->serio_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
								NULL, module->conv);
		}
		else if (!module->of_table && streq(name, "__mod_of_device_table")) {
			module->of_size = PERBIT(OF_DEVICE_SIZE);
			module->of_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							     NULL, module->conv);
		}
	}
}

struct module_ops PERBIT(mod_ops) = {
	.load_symbols	= PERBIT(load_symbols),
	.load_dep_syms	= PERBIT(load_dep_syms),
	.fetch_tables	= PERBIT(fetch_tables),
	.get_aliases	= PERBIT(get_aliases),
	.get_modinfo	= PERBIT(get_modinfo),
};
