#include <stdint.h>
#include <stddef.h>
struct cdbw;
struct cdbw *cdbw_open(void);
int cdbw_put(struct cdbw *, const void *, size_t, const void *, size_t);
int cdbw_put_data(struct cdbw *, const void *, size_t, uint32_t *);
int cdbw_put_key(struct cdbw *, const void *, size_t, uint32_t);
int cdbw_output(struct cdbw *, int, const char *, uint32_t (*)(void));
void cdbw_close(struct cdbw *);
uint32_t cdbw_stable_seeder(void);
