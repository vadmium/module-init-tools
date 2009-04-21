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

