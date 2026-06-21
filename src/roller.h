#pragma once
#include "types.h"

/*
 * Roller-RAM bitmap decoder — STUB.
 *
 * The PCW screen is described by a 512-byte "roller RAM" table
 * (256 little-endian pointers, one per visible scan line) plus a
 * scroll-Y register; each pointer locates a 90-byte bitmap row
 * inside main RAM, with bytes interleaved using the formula
 *
 *     byte_offset = (off & 7) + 2 * (off & 0x1FF8)
 *
 * (see `bin/JoycePcwTerm.cxx::do_roller` in joyce-custom). For the
 * scaffold pass roller_render() just clears the framebuffer to
 * "pen off" — enough to confirm the SDL pipeline and tint code
 * work. The real decoder lands in a follow-up.
 */

struct Mem;
struct Asic;
struct Display;

void roller_render(struct Mem *m, struct Asic *a, struct Display *d);
