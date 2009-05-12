#if defined(ELF32BIT)

#define PERBIT(x) x##32
#define ElfPERBIT(x) Elf32_##x
#define ELFPERBIT(x) ELF32_##x

#elif defined(ELF64BIT)

#define PERBIT(x) x##64
#define ElfPERBIT(x) Elf64_##x
#define ELFPERBIT(x) ELF64_##x

#else
#  error "Undefined ELF word length"
#endif

void *PERBIT(get_section)(void *file,
			  unsigned long fsize,
			  const char *secname,
			  unsigned long *secsize,
			  int conv)
{
	ElfPERBIT(Ehdr) *hdr;
	ElfPERBIT(Shdr) *sechdrs;
	ElfPERBIT(Off) e_shoff;
	ElfPERBIT(Half) e_shnum, e_shstrndx;

	const char *secnames;
	unsigned int i;

	if (fsize > 0 && fsize < sizeof(*hdr))
		return NULL;

	hdr = file;
	e_shoff = END(hdr->e_shoff, conv);
	e_shnum = END(hdr->e_shnum, conv);
	e_shstrndx = END(hdr->e_shstrndx, conv);

	if (fsize > 0 && fsize < e_shoff + e_shnum * sizeof(sechdrs[0]))
		return NULL;

	sechdrs = file + e_shoff;

	if (fsize > 0 && fsize < END(sechdrs[e_shstrndx].sh_offset, conv))
		return NULL;

	/* Find section by name, return pointer and size. */

	secnames = file + END(sechdrs[e_shstrndx].sh_offset, conv);
	for (i = 1; i < e_shnum; i++) {
		if (streq(secnames + END(sechdrs[i].sh_name, conv), secname)) {
			*secsize = END(sechdrs[i].sh_size, conv);
			return file + END(sechdrs[i].sh_offset, conv);
		}
	}
	*secsize = 0;
	return NULL;
}

/* Load the given section: NULL on error. */
static void *PERBIT(load_section)(struct elf_file *module,
				  const char *secname,
				  unsigned long *secsize)
{
	return PERBIT(get_section)(module->data, 0, secname, secsize, module->conv);
}

static struct string_table *PERBIT(load_strings)(struct elf_file *module,
						 const char *secname,
						 struct string_table *tbl)
{
	unsigned long size;
	const char *strings;

	strings = PERBIT(load_section)(module, secname, &size);
	if (strings) {
		/* Skip any zero padding. */
		while (!strings[0]) {
			strings++;
			if (size-- <= 1)
				return tbl;
		}
		for (; strings; strings = next_string(strings, &size))
			tbl = NOFAIL(strtbl_add(strings, tbl));
	}
	return tbl;
}

static struct string_table *PERBIT(load_symbols)(struct elf_file *module)
{
	struct PERBIT(kernel_symbol) *ksyms;
	struct string_table *symtbl;
	unsigned long i, size;

	symtbl = NULL;

	/* New-style: strings are in this section. */
	symtbl = PERBIT(load_strings)(module, "__ksymtab_strings", symtbl);
	if (symtbl) {
		/* GPL symbols too */
		return PERBIT(load_strings)(module, "__ksymtab_strings_gpl",
			symtbl);
	}

	/* Old-style. */
	ksyms = PERBIT(load_section)(module, "__ksymtab", &size);
	for (i = 0; i < size / sizeof(struct PERBIT(kernel_symbol)); i++)
		symtbl = NOFAIL(strtbl_add(ksyms[i].name, symtbl));
	ksyms = PERBIT(load_section)(module, "__gpl_ksymtab", &size);
	for (i = 0; i < size / sizeof(struct PERBIT(kernel_symbol)); i++)
		symtbl = NOFAIL(strtbl_add(ksyms[i].name, symtbl));

	return symtbl;
}

static char *PERBIT(get_aliases)(struct elf_file *module, unsigned long *size)
{
	return PERBIT(load_section)(module, ".modalias", size);
}

static char *PERBIT(get_modinfo)(struct elf_file *module, unsigned long *size)
{
	return PERBIT(load_section)(module, ".modinfo", size);
}

#ifndef STT_REGISTER
#define STT_REGISTER    13              /* Global register reserved to app. */
#endif

static struct string_table *PERBIT(load_dep_syms)(const char *pathname,
						  struct elf_file *module,
						  struct string_table **types)
{
	unsigned int i;
	unsigned long size;
	char *strings;
	ElfPERBIT(Sym) *syms;
	ElfPERBIT(Ehdr) *hdr;
	int handle_register_symbols;
	struct string_table *names;
	int conv;

	names = NULL;
	*types = NULL;

	strings = PERBIT(load_section)(module, ".strtab", &size);
	syms = PERBIT(load_section)(module, ".symtab", &size);

	if (!strings || !syms) {
		warn("Couldn't find symtab and strtab in module %s\n",
		     pathname);
		return NULL;
	}

	hdr = module->data;
	conv = module->conv;

	handle_register_symbols =
		(END(hdr->e_machine, conv) == EM_SPARC ||
		 END(hdr->e_machine, conv) == EM_SPARCV9);

	for (i = 1; i < size / sizeof(syms[0]); i++) {
		if (END(syms[i].st_shndx, conv) == SHN_UNDEF) {
			/* Look for symbol */
			const char *name;
			int weak;

			name = strings + END(syms[i].st_name, conv);

			/* Not really undefined: sparc gcc 3.3 creates
                           U references when you have global asm
                           variables, to avoid anyone else misusing
                           them. */
			if (handle_register_symbols
			    && (ELFPERBIT(ST_TYPE)(END(syms[i].st_info, conv))
				== STT_REGISTER))
				continue;

			weak = (ELFPERBIT(ST_BIND)(END(syms[i].st_info, conv))
				== STB_WEAK);
			names = strtbl_add(name, names);
			*types = strtbl_add(weak ? weak_sym : undef_sym, *types);
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
static void PERBIT(fetch_tables)(struct elf_file *module,
				 struct module_tables *tables)
{
	unsigned int i;
	unsigned long size;
	char *strings;
	ElfPERBIT(Ehdr) *hdr;
	ElfPERBIT(Sym) *syms;
	ElfPERBIT(Shdr) *sechdrs;
	int conv;

	hdr = module->data;
	conv = module->conv;

	sechdrs = (void *)hdr + END(hdr->e_shoff, conv);
	strings = PERBIT(load_section)(module, ".strtab", &size);
	syms = PERBIT(load_section)(module, ".symtab", &size);

	/* Don't warn again: we already have above */
	if (!strings || !syms)
		return;

	tables->pci_table = NULL;
	tables->usb_table = NULL;
	tables->ccw_table = NULL;
	tables->ieee1394_table = NULL;
	tables->pnp_table = NULL;
	tables->pnp_card_table = NULL;
	tables->input_table = NULL;
	tables->serio_table = NULL;
	tables->of_table = NULL;

	for (i = 0; i < size / sizeof(syms[0]); i++) {
		char *name = strings + END(syms[i].st_name, conv);

		if (!tables->pci_table && streq(name, "__mod_pci_device_table")) {
			tables->pci_size = PERBIT(PCI_DEVICE_SIZE);
			tables->pci_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, conv);
		}
		else if (!tables->usb_table && streq(name, "__mod_usb_device_table")) {
			tables->usb_size = PERBIT(USB_DEVICE_SIZE);
			tables->usb_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, conv);
		}
		else if (!tables->ccw_table && streq(name, "__mod_ccw_device_table")) {
			tables->ccw_size = PERBIT(CCW_DEVICE_SIZE);
			tables->ccw_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, conv);
		}
		else if (!tables->ieee1394_table && streq(name, "__mod_ieee1394_device_table")) {
			tables->ieee1394_size = PERBIT(IEEE1394_DEVICE_SIZE);
			tables->ieee1394_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
								   NULL, conv);
		}
		else if (!tables->pnp_table && streq(name, "__mod_pnp_device_table")) {
			tables->pnp_size = PERBIT(PNP_DEVICE_SIZE);
			tables->pnp_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							      NULL, conv);
		}
		else if (!tables->pnp_card_table && streq(name, "__mod_pnp_card_device_table")) {
			tables->pnp_card_size = PERBIT(PNP_CARD_DEVICE_SIZE);
			tables->pnp_card_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
								   NULL, conv);
			tables->pnp_card_offset = PERBIT(PNP_CARD_DEVICE_OFFSET);
		}
		else if (!tables->input_table && streq(name, "__mod_input_device_table")) {
			tables->input_size = PERBIT(INPUT_DEVICE_SIZE);
			tables->input_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							        &tables->input_table_size,
							        conv);
		}
		else if (!tables->serio_table && streq(name, "__mod_serio_device_table")) {
			tables->serio_size = PERBIT(SERIO_DEVICE_SIZE);
			tables->serio_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
								NULL, conv);
		}
		else if (!tables->of_table && streq(name, "__mod_of_device_table")) {
			tables->of_size = PERBIT(OF_DEVICE_SIZE);
			tables->of_table = PERBIT(deref_sym)(hdr, sechdrs, &syms[i],
							     NULL, conv);
		}
	}
}

struct module_ops PERBIT(mod_ops) = {
	.load_strings	= PERBIT(load_strings),
	.load_symbols	= PERBIT(load_symbols),
	.load_dep_syms	= PERBIT(load_dep_syms),
	.fetch_tables	= PERBIT(fetch_tables),
	.get_aliases	= PERBIT(get_aliases),
	.get_modinfo	= PERBIT(get_modinfo),
};

#undef PERBIT
#undef ElfPERBIT
#undef ELFPERBIT
