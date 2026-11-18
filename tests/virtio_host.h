#ifndef VIRTIO_HOST_H
#define VIRTIO_HOST_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

typedef uint64_t paddr_t;
extern uint64_t hhdm_offset;
#define P2V(pa) ((void *)(hhdm_offset + (uint64_t)(pa)))

void     outb(uint16_t port, uint8_t v);
void     outw(uint16_t port, uint16_t v);
void     outl(uint16_t port, uint32_t v);
uint8_t  inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
paddr_t  pmm_alloc_contig(unsigned pages);
paddr_t  pmm_alloc_zero(void);
void     kprintf(const char *fmt, ...);
#endif
