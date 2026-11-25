# Porting notes

## NVMe in the installer
- Disks are probed as `sdX`, `vdX` and `nvme<ctrl>n<ns>` (controllers 0-3, namespaces 1-4) through one `probe_disk()` path in `bin/install/src/engine.c`.
- Partitions on NVMe use the `p` separator (`nvme0n1p1`); devfs, lsblk and the installer share this rule.

## Swap
- Format: Linux SWAPSPACE2 (version 1, magic at byte 4086, no bad-page list), so `mkswap` output interoperates.
- Unit: 4 KiB page = 8 sectors. A swapped PTE is not-present with `PTE_SWAP` (bit 9) and the slot number in the address bits.
- Slots carry a refcount map (`swap_dup` on fork, `swap_free` on unmap/exit).
- Eviction: second chance using `PTE_A`, driven by a reclaim hook inside `pmm_alloc`. Swap-in happens in the page-fault handler and in `uaccess` copies.
- Block drivers poll and syscalls run with IF masked, so swap I/O is safe inside the fault path.
- `proc_brk` allocates eagerly, so heap growth under pressure evicts other pages instead of failing later.
- `swapoff` pulls every slot back into RAM and returns EBUSY if any slot stays referenced.
- Swap areas overlapping the root device are refused.
- `init` runs `swapon` for the `PARTUUID=` entry in `/etc/swap`, which the installer already writes.
- Limitation: one swap area at a time.

## Mouse
- PS/2 aux port through the i8042, IRQ12; 3-byte packets, 4-byte when the IntelliMouse wheel (ID 3) is detected.
- The keyboard IRQ handler forwards bytes with status bit 0x20 to `mouse_byte`.
- `/dev/mouse` delivers `struct nir_mouse_event` records (dx, dy down-positive, wheel, buttons).

## fbcon, gpmd
- fbcon keeps a text-cell mirror of the screen and draws pointer and selection as XOR overlays, so nothing is lost when they move.
- Screen text is read through `NIR_FBREAD` because VCHR nodes reject lseek.
- Paste uses root-only `TIOCSTI`.
- The pointer is tracked in pixels at one pixel per mickey (`-s` scales it) and the highlighted cell is the one under that pixel.
- `gpmd` is split into `mouse.c` (device), `pointer.c` (position and overlay), `select.c` (selection), `console.c` (fbcon and paste).
- Limitations: no gpm client socket protocol, no wheel scrollback, selection highlight clears on any console output.

## Known gaps
- Only the first virtio-blk disk is probed by the kernel (pre-existing).
- gpmd, mouse events and the installer NVMe UI were not exercised end to end.
