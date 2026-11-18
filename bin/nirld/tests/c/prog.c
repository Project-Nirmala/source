static int table[8] = {3, 1, 4, 1, 5, 9, 2, 6};
static const char *names[] = {"zero", "one", "two", "three"};
int gcounter = 7;
int gzero[16];
extern int ext_val(int);
static int (*fp[3])(int);
static int a(int x) { return x + 1; }
static int b(int x) { return x * 2; }
static int c(int x) { return x - 3; }
static int sw(int x)
{
	switch (x) {
	case 0: return 11;
	case 1: return 22;
	case 2: return 33;
	case 3: return 44;
	case 4: return 55;
	case 5: return 66;
	case 6: return 77;
	default: return 88;
	}
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
int main(void)
{
	int i, sum;
	fp[0] = a; fp[1] = b; fp[2] = c;
	sum = 0;
	for (i = 0; i < 8; i++)
		sum += table[i];
	for (i = 0; i < 3; i++)
		sum += fp[i](i);
	for (i = 0; i < 4; i++)
		sum += slen(names[i]) + sw(i);
	gzero[3] = gcounter;
	sum += gzero[3] + ext_val(5);
	return sum & 0xff;
}
