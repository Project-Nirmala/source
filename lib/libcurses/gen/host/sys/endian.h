#ifndef _TIC_ENDIAN_H
#define _TIC_ENDIAN_H
#include <stdint.h>
static inline void le16enc(void *p, uint16_t v) { uint8_t *b = p; b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static inline uint16_t le16dec(const void *p) { const uint8_t *b = p; return (uint16_t)(b[0] | b[1] << 8); }
static inline void le32enc(void *p, uint32_t v) { le16enc(p, (uint16_t)v); le16enc((uint8_t *)p + 2, (uint16_t)(v >> 16)); }
static inline uint32_t le32dec(const void *p) { return le16dec(p) | (uint32_t)le16dec((const uint8_t *)p + 2) << 16; }
#endif
