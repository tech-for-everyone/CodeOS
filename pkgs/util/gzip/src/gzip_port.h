/* gzip port — shared declarations.
 *
 * The DEFLATE decoder in inflate.c is adapted from pkgs/core/codec's
 * codec_inflate.c (the same puff-style canonical Huffman strategy). It is
 * standalone here so gzip can be built without pulling in the codec package.
 */
#ifndef GZIP_PORT_H
#define GZIP_PORT_H

#include <stdint.h>
#include <stddef.h>

/* Fixed ceiling imposed by the kernel's file API (FS_CONTENT_MAX). */
#define GZIP_MAX_FILE 65536

/* Inflate a raw DEFLATE stream. On success returns 0, sets *out_len to the
 * number of bytes produced and, when consumed is non-null, the number of
 * input bytes used (so the caller can find the gzip trailer). Negative on
 * error, with -1 meaning "truncated input". */
int gzip_inflate(const uint8_t *src, size_t src_len,
                 uint8_t *out, size_t *out_len, size_t *consumed);

/* CRC-32 (IEEE 802.3), used by the gzip trailer. */
uint32_t gzip_crc32(const uint8_t *data, size_t len);

#endif /* GZIP_PORT_H */
