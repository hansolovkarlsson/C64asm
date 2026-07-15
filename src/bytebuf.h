/*
 * bytebuf.h - a small growable byte buffer, used to accumulate the
 * assembled machine code during pass 2.
 *
 * Unlike almost everything else in this codebase (see the note at the
 * top of common.h about fixed-size buffers being the norm here), the
 * assembled output genuinely can't be given a sensible fixed maximum
 * size in advance -- a source file could reasonably assemble to
 * anywhere from a few bytes to tens of kilobytes. This is the one place
 * a classic dynamic-array-that-doubles-when-full growth strategy earns
 * its complexity.
 */

#ifndef C64ASM_BYTEBUF_H
#define C64ASM_BYTEBUF_H

#include <stddef.h>

typedef struct {
    unsigned char *data;
    size_t len;   /* bytes currently stored */
    size_t cap;   /* bytes currently allocated (>= len) */
} ByteBuf;

/* Initializes an empty buffer. Must be called before any bb_push*()
 * call -- there's no implicit "first use" initialization. */
void bb_init(ByteBuf *b);

/* Appends a single byte, growing the buffer (by doubling its capacity)
 * if it's already full. Doubling, rather than growing by a fixed
 * amount each time, is what keeps repeated pushes cheap on average: it
 * means the *number* of reallocations needed to reach N total bytes is
 * proportional to log(N), not to N itself. */
void bb_push(ByteBuf *b, unsigned char v);

/* Appends n bytes at once -- just a convenience wrapper around calling
 * bb_push() n times. */
void bb_push_n(ByteBuf *b, const unsigned char *v, size_t n);

#endif
