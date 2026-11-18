int used_data = 3;
int unused_data = 4;
int used_fn(void) { return 7; }
int unused_fn(void) { return unused_data + 9; }
int main(void) { return used_fn() + used_data; }
