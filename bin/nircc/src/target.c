#include "cc.h"

static const struct target t64 = {
	64, 8, 'q',
	"%rax", "%rbx", "%rcx", "%rdx", "%rdi", "%rsi", "%rbp", "%rsp",
	"--64", "elf_x86_64", "-m64"
};

static const struct target t32 = {
	32, 4, 'l',
	"%eax", "%ebx", "%ecx", "%edx", "%ecx", "%esi", "%ebp", "%esp",
	"--32", "elf_i386", "-m32"
};

const struct target *tg = &t64;

void target_set(int bits)
{
	tg = bits == 32 ? &t32 : &t64;
}

int host_bits(void)
{
	return (int)sizeof(void *) * 8;
}
