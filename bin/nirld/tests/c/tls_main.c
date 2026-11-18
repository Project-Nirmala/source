__thread int tv = 5;
__thread int tz;
__thread char tarr[10] = { 1, 2, 3 };
extern __thread int ext_tls;

int main(void)
{
	tz = 3;
	tv += 2;
	ext_tls += 1;
	return tv * 10 + tz + ext_tls + tarr[2];
}
