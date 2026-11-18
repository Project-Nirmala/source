extern char __ehdr_start[];
static char tlsbuf[1024] __attribute__((aligned(64)));

static unsigned long rdn(const unsigned char *p, int n)
{
	unsigned long v;
	int i;

	v = 0;
	for (i = n - 1; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

void tls_setup(void)
{
	const unsigned char *eh;
	const unsigned char *ph;
	const unsigned char *src;
	unsigned long phoff, phsz, phnum, vaddr, filesz, memsz, align, size, tp, start, i;

	eh = (const unsigned char *)__ehdr_start;
	vaddr = filesz = memsz = 0;
	align = 1;
#ifdef __x86_64__
	phoff = rdn(eh + 32, 8);
	phsz = rdn(eh + 54, 2);
	phnum = rdn(eh + 56, 2);
#else
	phoff = rdn(eh + 28, 4);
	phsz = rdn(eh + 42, 2);
	phnum = rdn(eh + 44, 2);
#endif
	for (i = 0; i < phnum; i++) {
		ph = eh + phoff + i * phsz;
		if (rdn(ph, 4) != 7)
			continue;
#ifdef __x86_64__
		vaddr = rdn(ph + 16, 8);
		filesz = rdn(ph + 32, 8);
		memsz = rdn(ph + 40, 8);
		align = rdn(ph + 48, 8);
#else
		vaddr = rdn(ph + 8, 4);
		filesz = rdn(ph + 16, 4);
		memsz = rdn(ph + 20, 4);
		align = rdn(ph + 28, 4);
#endif
	}
	size = (memsz + align - 1) & ~(align - 1);
	tp = ((unsigned long)tlsbuf + 512 + 63) & ~63UL;
	start = tp - size;
	src = (const unsigned char *)vaddr;
	for (i = 0; i < filesz; i++)
		((unsigned char *)start)[i] = src[i];
	*(unsigned long *)tp = tp;
#ifdef __x86_64__
	{
		long ret;

		__asm__ volatile("syscall" : "=a"(ret) : "0"(158L), "D"(0x1002L), "S"(tp)
				 : "rcx", "r11", "memory");
	}
#else
	{
		unsigned int ud[4];
		int ret;

		ud[0] = (unsigned int)-1;
		ud[1] = (unsigned int)tp;
		ud[2] = 0xfffff;
		ud[3] = 0x51;
		__asm__ volatile("int $0x80" : "=a"(ret) : "0"(243), "b"(ud) : "memory");
		__asm__ volatile("movw %w0, %%gs" : : "r"((unsigned short)((ud[0] << 3) | 3)));
	}
#endif
}
