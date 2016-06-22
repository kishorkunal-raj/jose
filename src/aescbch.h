/* vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab smarttab colorcolumn=80: */

#include <openssl/evp.h>
#include <stdbool.h>

uint8_t *
aescbch_encrypt(const char *alg, EVP_PKEY *key, const uint8_t pt[], size_t ptl,
                size_t *ivl, size_t *ctl, size_t *tgl, ...);


ssize_t
aescbch_decrypt(const char *alg, EVP_PKEY *key, const uint8_t iv[], size_t ivl,
                const uint8_t ct[], size_t ctl, const uint8_t tg[], size_t tgl,
                uint8_t pt[], ...);
