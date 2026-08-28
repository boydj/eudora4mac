/* MD5 message digest + HMAC-MD5 — the modern md5.c (RSA reference
 * implementation lineage, RFC 1321 / RFC 2104).  Used by APOP and
 * CRAM-MD5 authentication. */

#ifndef EUDORA_MD5_H
#define EUDORA_MD5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[4];
    uint64_t count; /* bits processed */
    unsigned char buffer[64];
} EudoraMD5Context;

void eudora_md5_init(EudoraMD5Context *ctx);
void eudora_md5_update(EudoraMD5Context *ctx, const void *data, size_t len);
void eudora_md5_final(EudoraMD5Context *ctx, unsigned char digest[16]);

void eudora_md5(const void *data, size_t len, unsigned char digest[16]);

/* hmac_md5 (md5.c) — RFC 2104. */
void eudora_hmac_md5(const void *text, size_t text_len, const void *key,
                     size_t key_len, unsigned char digest[16]);

#ifdef __cplusplus
}
#endif

#endif /* EUDORA_MD5_H */
