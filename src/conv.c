/* vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab smarttab colorcolumn=80: */

#include "conv.h"
#include "b64.h"

#include <openssl/evp.h>
#include <string.h>

BIGNUM *
bn_decode(const uint8_t buf[], size_t len)
{
    return BN_bin2bn(buf, len, NULL);
}

BIGNUM *
bn_decode_json(const json_t *json)
{
    uint8_t *buf = NULL;
    BIGNUM *bn = NULL;
    size_t len = 0;

    if (!json_is_string(json))
        return NULL;

    len = jose_b64_dlen(json_string_length(json));
    buf = malloc(len);
    if (!buf)
        return NULL;

    if (jose_b64_decode(json_string_value(json), buf))
        bn = bn_decode(buf, len);

    memset(buf, 0, len);
    free(buf);
    return bn;
}

bool
bn_encode(const BIGNUM *bn, uint8_t buf[], size_t len)
{
    int bytes = 0;

    if (!bn)
        return false;

    if (len == 0)
        len = BN_num_bytes(bn);

    bytes = BN_num_bytes(bn);
    if (bytes < 0 || bytes > (int) len)
        return false;

    memset(buf, 0, len);
    return BN_bn2bin(bn, &buf[len - bytes]) > 0;
}

json_t *
bn_encode_json(const BIGNUM *bn, size_t len)
{
    uint8_t *buf = NULL;
    json_t *out = NULL;

    if (!bn)
        return false;

    if (len == 0)
        len = BN_num_bytes(bn);

    if ((int) len < BN_num_bytes(bn))
        return false;

    buf = malloc(len);
    if (buf) {
        if (bn_encode(bn, buf, len))
            out = jose_b64_encode_json(buf, len);

        free(buf);
    }

    return out;
}

json_t *
compact_to_obj(const char *compact, ...)
{
    json_t *out = NULL;
    size_t count = 0;
    size_t c = 0;
    va_list ap;

    if (!compact)
        return NULL;

    va_start(ap, compact);
    while (va_arg(ap, const char *))
        count++;
    va_end(ap);

    size_t len[count];

    memset(len, 0, sizeof(len));

    for (size_t i = 0; compact[i]; i++) {
        if (compact[i] != '.')
            len[c]++;
        else if (++c > count - 1)
            return NULL;
    }

    if (c != count - 1)
        return NULL;

    out = json_object();
    if (!out)
        return NULL;

    c = 0;
    va_start(ap, compact);
    for (size_t i = 0; i < count; i++) {
        json_t *val = json_stringn(&compact[c], len[i]);
        if (json_object_set_new(out, va_arg(ap, const char *), val) < 0) {
            json_decref(out);
            va_end(ap);
            return NULL;
        }

        c += len[i] + 1;
    }
    va_end(ap);

    if (json_object_size(out) == 0) {
        json_decref(out);
        return NULL;
    }

    return out;
}

size_t
str_to_enum(const char *str, ...)
{
    size_t i = 0;
    va_list ap;

    va_start(ap, str);

    for (const char *v = NULL; (v = va_arg(ap, const char *)); i++) {
        if (str && strcmp(str, v) == 0)
            break;
    }

    va_end(ap);
    return i;
}

bool
has_flags(const char *flags, bool all, const char *query)
{
    if (!flags || !query)
        return false;

    for (size_t i = 0; query[i]; i++) {
        const char *c = strchr(flags, query[i]);
        if (all && !c)
            return false;
        if (!all && c)
            return true;
    }

    return all;
}

json_t *
encode_protected(json_t *obj)
{
    json_t *p = NULL;

    if (json_unpack(obj, "{s?o}", "protected", &p) == -1)
        return false;

    if (!p)
        return json_string("");

    if (json_is_string(p))
        return json_incref(p);

    if (!json_is_object(p))
        return NULL;

    p = jose_b64_encode_json_dump(p);
    if (!p)
        return NULL;

    if (json_object_set_new(obj, "protected", p) == -1)
        return NULL;

    return p;
}

/*
 * This really doesn't belong here, but OpenSSL doesn't (yet) help us.
 *
 * I have submitted a version of this function upstream:
 *   https://github.com/openssl/openssl/pull/1217
 */
const unsigned char *
EVP_PKEY_get0_hmac(EVP_PKEY *pkey, size_t *len)
{
    ASN1_OCTET_STRING *os = NULL;

    if (EVP_PKEY_base_id(pkey) != EVP_PKEY_HMAC)
        return NULL;

    os = EVP_PKEY_get0(pkey);
    *len = os->length;
    return os->data;
}

bool
add_entity(json_t *root, json_t *obj, const char *plural)
{
    bool found = false;
    json_t *pl = NULL;

    pl = json_object_get(root, plural);
    if (pl) {
        if (!json_is_array(pl))
            return false;

        if (json_array_size(pl) == 0) {
            if (json_object_del(root, plural) == -1)
                return false;

            pl = NULL;
        }
    }

    for (void *i = json_object_iter(obj); i && !found;
               i = json_object_iter_next(obj, i)) {
        if (json_object_get(root, json_object_iter_key(i)))
            found = true;
    }

    /* If we have flattened format, migrate to general format. */
    if (found) {
        json_t *o = NULL;

        if (!pl) {
            pl = json_array();
            if (json_object_set_new(root, plural, pl) == -1)
                return false;
        }

        o = json_object();
        if (json_array_append_new(pl, o) == -1)
            return false;

        for (void *i = json_object_iter(obj); i && !found;
                   i = json_object_iter_next(obj, i)) {
            const char *key = json_object_iter_key(i);
            json_t *tmp = NULL;

            tmp = json_object_get(root, key);
            if (tmp) {
                if (json_object_set(o, key, tmp) == -1)
                    return false;

                if (json_object_del(o, key) == -1)
                    return false;
            }
        }
    }

    /* If we have some signatures already, append to the array. */
    if (pl)
        return json_array_append(pl, obj) == 0;

    return json_object_update(root, obj) == 0;
}

