/* Raw DEFLATE decompressor (RFC 1951), adapted from pkgs/core/codec.
 *
 * The original lives in core/codec/src/codec_inflate.c and is used to decode
 * PNG image data. It is copied here because a gzip package must not depend on
 * another package's private codec API. Only the error reporting and the
 * `consumed` output were changed; the Huffman logic is identical.
 */

#include "gzip_port.h"

#define MAXBITS 15
#define HUFF_SYMS 288

typedef struct {
    short count[MAXBITS + 1];
    short symbol[HUFF_SYMS];
} huff_t;

typedef struct {
    const uint8_t *in;
    size_t         inlen;
    size_t         incnt;
    int            bitbuf;
    int            bitcnt;
    uint8_t       *out;
    size_t         outlen;
    size_t         outcnt;
} inflate_t;

static int bits(inflate_t *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (long)(s->in[s->incnt++]) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static void emit(inflate_t *s, int byte) {
    if (s->outcnt >= s->outlen) return;
    s->out[s->outcnt++] = (uint8_t)byte;
}

static int decode_sym(inflate_t *s, const huff_t *h) {
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        int b = bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int construct(huff_t *h, const short *length, int n) {
    int symbol, len, left;
    short offs[MAXBITS + 1];

    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;

    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int stored_block(inflate_t *s) {
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    len = s->in[s->incnt++];
    len |= (unsigned)s->in[s->incnt++] << 8;
    nlen = s->in[s->incnt++];
    nlen |= (unsigned)s->in[s->incnt++] << 8;
    if (len != ((~nlen) & 0xFFFF)) return -2;
    if (s->incnt + len > s->inlen) return -1;
    for (unsigned i = 0; i < len; i++) emit(s, s->in[s->incnt++]);
    return 0;
}

static int codes_block(inflate_t *s, const huff_t *lencode, const huff_t *distcode) {
    static const short lens[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const short lext[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const short dists[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const short dext[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    for (;;) {
        int symbol = decode_sym(s, lencode);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            emit(s, symbol);
        } else if (symbol == 256) {
            return 0;
        } else {
            int extra, len;
            unsigned dist;
            symbol -= 257;
            if (symbol >= 29) return -3;
            extra = bits(s, lext[symbol]);
            if (extra < 0) return -1;
            len = lens[symbol] + extra;

            symbol = decode_sym(s, distcode);
            if (symbol < 0) return -1;
            if (symbol >= 30) return -4;
            extra = bits(s, dext[symbol]);
            if (extra < 0) return -1;
            dist = (unsigned)(dists[symbol] + extra);
            if (dist > s->outcnt) return -5;

            for (int i = 0; i < len; i++)
                emit(s, s->out[s->outcnt - dist]);
        }
    }
}

static int fixed_block(inflate_t *s) {
    static short lensym[288], distsym[30];
    static huff_t lencode, distcode;
    static int built;
    if (!built) {
        int symbol;
        for (symbol = 0; symbol < 144; symbol++) lensym[symbol] = 8;
        for (; symbol < 256; symbol++)          lensym[symbol] = 9;
        for (; symbol < 280; symbol++)          lensym[symbol] = 7;
        for (; symbol < 288; symbol++)          lensym[symbol] = 8;
        construct(&lencode, lensym, 288);
        for (symbol = 0; symbol < 30; symbol++) distsym[symbol] = 5;
        construct(&distcode, distsym, 30);
        built = 1;
    }
    return codes_block(s, &lencode, &distcode);
}

static int dynamic_block(inflate_t *s) {
    static const short order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    int nlen, ndist, ncode, index, err;
    short lengths[288 + 30];
    huff_t lencode, distcode;

    nlen  = bits(s, 5); if (nlen  < 0) return -1;
    ndist = bits(s, 5); if (ndist < 0) return -1;
    ncode = bits(s, 4); if (ncode < 0) return -1;
    nlen  += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) return -6;

    for (index = 0; index < ncode; index++) {
        int v = bits(s, 3);
        if (v < 0) return -1;
        lengths[order[index]] = (short)v;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;

    err = construct(&lencode, lengths, 19);
    if (err != 0) return -7;

    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode_sym(s, &lencode);
        if (symbol < 0) return -1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            short len = 0;
            int n;
            if (symbol == 16) {
                if (index == 0) return -8;
                len = lengths[index - 1];
                n = bits(s, 2); if (n < 0) return -1;
                n += 3;
            } else if (symbol == 17) {
                n = bits(s, 3); if (n < 0) return -1;
                n += 3;
            } else {
                n = bits(s, 7); if (n < 0) return -1;
                n += 11;
            }
            if (index + n > nlen + ndist) return -9;
            while (n--) lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) return -10;

    err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -11;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -13;
    return codes_block(s, &lencode, &distcode);
}

int gzip_inflate(const uint8_t *src, size_t src_len,
                 uint8_t *out, size_t *out_len, size_t *consumed) {
    inflate_t s;
    int last, type, err = 0;
    if (!src || !out || !out_len) return -99;

    s.in = src; s.inlen = src_len; s.incnt = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    s.out = out; s.outlen = *out_len; s.outcnt = 0;

    do {
        last = bits(&s, 1);
        if (last < 0) { err = -1; break; }
        type = bits(&s, 2);
        if (type < 0) { err = -1; break; }
        if (type == 0)      err = stored_block(&s);
        else if (type == 1) err = fixed_block(&s);
        else if (type == 2) err = dynamic_block(&s);
        else                err = -20;
        if (err != 0) break;
    } while (!last);

    if (err != 0) return err;
    *out_len = s.outcnt;
    if (consumed) *consumed = s.incnt;
    return 0;
}
